#include <algorithm>
#include <cmath>
#include <deque>

#include "tb/analyzer.hpp"

namespace tb {
namespace {
int64_t median(std::vector<int64_t> v) {
  if (v.empty()) return 0;
  std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
  return v[v.size() / 2];
}
}  // namespace

ClockModel estimate_clocks(const std::vector<Span>& spans) {
  ClockModel cm;
  std::unordered_map<std::string, const Span*> client_by_span;
  std::map<std::string, int> root_count;
  std::set<std::string> services;
  for (auto& s : spans) {
    services.insert(s.service);
    if (s.kind == "client") client_by_span[s.span] = &s;
    if (s.kind == "server" && s.parent.empty()) ++root_count[s.service];
  }
  std::map<std::pair<std::string, std::string>, std::vector<int64_t>> deltas;  // (caller, callee) -> samples
  for (auto& s : spans) {
    if (s.kind != "server" || s.parent.empty()) continue;
    auto it = client_by_span.find(s.parent);
    if (it == client_by_span.end() || it->second->service == s.service || it->second->trace != s.trace) continue;
    deltas[{it->second->service, s.service}].push_back(s.start - it->second->start);
  }
  // Reference clock: the service that receives external traffic (most root spans).
  for (auto& [svc, n] : root_count)
    if (cm.reference.empty() || n > root_count[cm.reference]) cm.reference = svc;
  if (cm.reference.empty() && !services.empty()) cm.reference = *services.begin();
  cm.offset_us[cm.reference] = 0;

  struct Link { std::string from, to; int64_t d; };  // offset[to] = offset[from] + d
  std::vector<Link> links;
  for (auto& [e, v] : deltas) {
    cm.edge_samples[e.first + "->" + e.second] = static_cast<int>(v.size());
    if (v.size() < 5) continue;
    const int64_t d = median(v);
    links.push_back({e.first, e.second, d});
    links.push_back({e.second, e.first, -d});
  }
  std::deque<std::string> q{cm.reference};
  while (!q.empty()) {
    const std::string cur = q.front();
    q.pop_front();
    for (auto& l : links)
      if (l.from == cur && !cm.offset_us.count(l.to)) {
        cm.offset_us[l.to] = cm.offset_us[cur] + l.d;
        q.push_back(l.to);
      }
  }
  for (auto& s : services)
    if (!cm.offset_us.count(s)) cm.offset_us[s] = 0;  // no causal link observed: assume aligned
  return cm;
}

void apply_clocks(Incident& inc, ClockModel& cm) {
  auto off = [&](const std::string& s) {
    auto it = cm.offset_us.find(s);
    return it == cm.offset_us.end() ? int64_t{0} : it->second;
  };
  for (auto& s : inc.spans) s.t = s.start - off(s.service);
  for (auto& l : inc.logs) l.t = l.ts - off(l.service);
  for (auto& m : inc.metrics) m.t = m.ts - off(m.service);

  // The ops system has its own clock. Anchor it with events the services also log themselves
  // (config reload, post-deploy start), matched by service.
  std::vector<int64_t> diffs;
  for (auto& o : inc.ops) {
    const char* want = o.type == "config_change" ? "config_changed" : (o.type == "deploy" && o.detail.value("phase", "") == "finished") ? "service_started" : nullptr;
    if (!want) continue;
    const Log* best = nullptr;
    for (auto& l : inc.logs) {
      if (l.service != o.service || l.event != want) continue;
      if (!best || std::llabs(l.t - o.ts) < std::llabs(best->t - o.ts)) best = &l;
    }
    if (best && std::llabs(best->t - o.ts) < 6000000) diffs.push_back(o.ts - best->t);
  }
  if (!diffs.empty()) {
    cm.ops_offset_us = median(diffs);
    cm.ops_calibrated = true;
  }
  for (auto& o : inc.ops) o.t = o.ts - cm.ops_offset_us;
}

Correlated correlate(const Incident& inc) {
  Correlated c;
  const auto& sp = inc.spans;
  c.parent.assign(sp.size(), -1);
  c.children.assign(sp.size(), {});
  c.virtual_parent.assign(sp.size(), false);
  std::unordered_map<std::string, int> by_span;
  for (size_t i = 0; i < sp.size(); ++i) by_span[sp[i].span] = static_cast<int>(i);
  for (size_t i = 0; i < sp.size(); ++i) {
    auto& s = sp[i];
    auto it = c.trace_of.find(s.trace);
    if (it == c.trace_of.end()) {
      it = c.trace_of.emplace(s.trace, static_cast<int>(c.traces.size())).first;
      c.traces.push_back({s.trace, {}, -1, false});
    }
    c.traces[it->second].spans.push_back(static_cast<int>(i));
  }
  for (auto& tr : c.traces) {
    std::sort(tr.spans.begin(), tr.spans.end(), [&](int a, int b) { return sp[a].t < sp[b].t; });
    for (int i : tr.spans) {
      auto& s = sp[i];
      if (s.parent.empty()) {
        if (s.kind == "server" && tr.root < 0) tr.root = i;
        continue;
      }
      auto it = by_span.find(s.parent);
      if (it != by_span.end() && sp[it->second].trace == s.trace) {
        c.parent[i] = it->second;
        continue;
      }
      // Parent span was lost. A server span must sit inside some other service's server span
      // in the same trace: attach to the tightest container (marked virtual).
      if (s.kind != "server") continue;
      int best = -1;
      for (int j : tr.spans) {
        auto& p = sp[j];
        if (j == i || p.kind != "server" || p.service == s.service) continue;
        if (p.t - 2000 <= s.t && s.t + s.dur <= p.t + p.dur + 2000 && (best < 0 || p.dur < sp[best].dur)) best = j;
      }
      if (best >= 0) { c.parent[i] = best; c.virtual_parent[i] = true; }
    }
    for (int i : tr.spans)
      if (c.parent[i] >= 0) c.children[c.parent[i]].push_back(i);
    if (tr.root >= 0) tr.failed = sp[tr.root].status >= 500;
  }
  return c;
}

std::vector<Edge> build_graph(const Incident& inc) {
  std::map<std::pair<std::string, std::string>, Edge> g;
  for (auto& s : inc.spans) {
    if (s.kind != "client" || s.peer.empty()) continue;
    Edge& e = g[{s.service, s.peer}];
    e.caller = s.service;
    e.callee = s.peer;
    ++e.calls;
    if (s.attempt > 1) ++e.retries;
    if (s.status == 0) { ++e.fail; ++e.timeouts; }
    else if (s.status < 0) { ++e.fail; ++e.refused; }
    else if (s.status >= 500) { ++e.fail; ++e.errors; }
  }
  std::vector<Edge> out;
  for (auto& [k, e] : g) out.push_back(e);
  return out;
}

CitationCheck validate_citations(const std::vector<Claim>& claims, const std::set<std::string>& valid_ids,
                                 const std::set<std::string>& supporting_ids) {
  CitationCheck r;
  for (auto& c : claims)
    for (auto& id : c.cites) {
      ++r.total;
      if (!valid_ids.count(id)) { r.invalid.push_back(id); continue; }
      ++r.existing;
      if (supporting_ids.count(id)) ++r.supported;
      else r.unsupported.push_back(id);
    }
  return r;
}

}  // namespace tb

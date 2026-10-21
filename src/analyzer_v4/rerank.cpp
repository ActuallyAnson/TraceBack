#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>

#include "tb/analyzer_v4.hpp"

namespace tb {
namespace {

constexpr size_t kSharedCallers = 4;  // a service called by this many distinct services is shared infrastructure

bool is_fail(const Span& s) { return s.kind == "server" ? s.status >= 500 : (s.status <= 0 || s.status >= 500); }

// Same measure as analyzer v2: per failed entry request, the services where its failure originates.
std::map<std::string, double> origin_share(const Incident& inc, const Correlated& cor, int& n_failed) {
  std::map<std::string, int> count;
  n_failed = 0;
  for (const Trace& tr : cor.traces) {
    if (!tr.failed || tr.root < 0) continue;
    ++n_failed;
    std::set<std::string> origins;
    std::vector<int> stack = {tr.root};
    while (!stack.empty()) {
      const int i = stack.back();
      stack.pop_back();
      bool child_fail = false;
      for (int c : cor.children[i])
        if (is_fail(inc.spans[c])) { child_fail = true; stack.push_back(c); }
      if (child_fail) continue;
      const Span& s = inc.spans[i];
      origins.insert(s.kind == "server" || s.peer.empty() ? s.service : s.peer);
    }
    for (auto& o : origins) ++count[o];
  }
  std::map<std::string, double> share;
  for (auto& [k, v] : count) share[k] = n_failed ? static_cast<double>(v) / n_failed : 0.0;
  return share;
}

// S2: a span the instrumentation itself flagged as an error, answered with 4xx, is a failure; so is a 4xx span whose direct child is one.
int normalize_error_flags(Incident& inc, const std::string& dir) {
  std::set<std::string> flagged;
  std::ifstream f(dir + "/spans.jsonl");
  std::string line;
  while (std::getline(f, line)) {
    if (line.find("\"err\":true") == std::string::npos) continue;
    try { flagged.insert(json::parse(line).value("id", "")); } catch (...) {}
  }
  if (flagged.empty()) return 0;
  int changed = 0;
  std::set<std::string> promoted;  // span ids promoted to failures
  for (auto& s : inc.spans)
    if (flagged.count(s.id) && s.status >= 400 && s.status < 500) { s.status = 500; promoted.insert(s.span); ++changed; }
  std::set<std::string> parents_of_promoted;
  for (auto& s : inc.spans)
    if (promoted.count(s.span) && !s.parent.empty()) parents_of_promoted.insert(s.parent);
  for (auto& s : inc.spans)
    if (s.status >= 400 && s.status < 500 && parents_of_promoted.count(s.span)) { s.status = 500; ++changed; }
  return changed;
}

// S3 (v4): a 4xx server span with a failing descendant is a failure. An application that answers 4xx while the work beneath it failed
// (the frontend turning a failed checkout into HTTP 422) hides the failure from the origin walk, which only follows failing spans.
int propagate_masked_failures(Incident& inc) {
  std::map<std::string, int> by_span;
  for (size_t i = 0; i < inc.spans.size(); ++i) by_span[inc.spans[i].span] = static_cast<int>(i);
  std::vector<std::vector<int>> kids(inc.spans.size());
  for (size_t i = 0; i < inc.spans.size(); ++i) {
    auto it = by_span.find(inc.spans[i].parent);
    if (!inc.spans[i].parent.empty() && it != by_span.end() && it->second != static_cast<int>(i)) kids[it->second].push_back(static_cast<int>(i));
  }
  std::vector<signed char> memo(inc.spans.size(), -1);  // 1: this span is, or has a descendant that is, a failure
  int changed = 0;
  // iterative post-order from every span without a retained parent
  for (size_t r = 0; r < inc.spans.size(); ++r) {
    if (!inc.spans[r].parent.empty() && by_span.count(inc.spans[r].parent)) continue;
    std::vector<std::pair<int, size_t>> st = {{static_cast<int>(r), 0}};
    while (!st.empty()) {
      auto& [i, k] = st.back();
      if (k < kids[i].size()) { const int c = kids[i][k++]; if (memo[c] < 0) st.push_back({c, 0}); continue; }
      bool sub = false;
      for (int c : kids[i]) sub = sub || memo[c] == 1;
      Span& s = inc.spans[i];
      if (sub && s.kind == "server" && s.status >= 400 && s.status < 500) { s.status = 500; ++changed; }
      memo[i] = (sub || is_fail(s)) ? 1 : 0;
      st.pop_back();
    }
  }
  return changed;
}

}  // namespace

json analyze_v4(Incident& inc, const std::string& dir) {
  const int promoted = normalize_error_flags(inc, dir);
  const int masked = propagate_masked_failures(inc);
  json rep = analyze_v2(inc);
  Correlated cor = correlate(inc);
  int n_failed = 0;
  const auto share = origin_share(inc, cor, n_failed);

  // S1: shared infrastructure (called by >= kSharedCallers distinct services) is not a root-cause candidate for a generic symptom
  // unless it originates at least half of the failed entry requests.
  std::map<std::string, std::set<std::string>> callers;
  for (auto& e : rep["graph"]) {
    const std::string a = e["caller"].get<std::string>(), b = e["callee"].get<std::string>();
    if (a != b) callers[b].insert(a);
  }
  json& hs = rep["hypotheses"];
  std::vector<json> v(hs.begin(), hs.end());
  bool demoted = false;
  for (auto& h : v) {
    const std::string cat = h["category"].get<std::string>(), svc = h["service"].get<std::string>();
    if (cat != "dependency_slow" && cat != "internal_errors") continue;
    if (!callers.count(svc) || callers[svc].size() < kSharedCallers) continue;
    const double s = share.count(svc) ? share.at(svc) : 0.0;
    if (s >= 0.5) continue;
    h["strength"] = h["strength"].get<double>() * 0.3;
    if (!h["features"].contains("v3_adjustments")) h["features"]["v3_adjustments"] = json::array();
    h["features"]["v3_adjustments"].push_back("demoted: " + svc + " is shared infrastructure (called by " + std::to_string(callers[svc].size()) + " services) and originates " +
                                              std::to_string(static_cast<int>(std::lround(s * 100))) + "% of failed requests");
    demoted = true;
  }
  if (demoted) {
    for (auto& h : v) h["score"] = h["strength"].get<double>() * (0.6 + 0.4 * h["coverage"].get<double>());
    std::stable_sort(v.begin(), v.end(), [](const json& a, const json& b) { return a["score"].get<double>() > b["score"].get<double>(); });
    int rank = 0;
    for (auto& h : v) {
      h["rank"] = ++rank;
      h["hyp_id"] = "H" + std::to_string(rank);
      h["strength"] = std::round(h["strength"].get<double>() * 1000) / 1000;
      h["score"] = std::round(h["score"].get<double>() * 1000) / 1000;
    }
    const double top = v[0]["score"].get<double>(), second = v.size() > 1 ? v[1]["score"].get<double>() : 0.0;
    rep["confidence"] = (top >= 0.65 && top - second >= 0.2) ? "high" : top >= 0.45 ? "medium" : "low";
    hs = json(v);
  }
  rep["analyzer_version"] = "v4";
  if (promoted) rep["symptom"]["error_flag_promotions"] = promoted;
  if (masked) rep["symptom"]["masked_failure_promotions"] = masked;
  return rep;
}

}  // namespace tb

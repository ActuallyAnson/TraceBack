#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "tb/analyzer_v2.hpp"

// Re-ranking over the v1 report. Rules are about causal structure and ordering only: where each failed request's
// failure originates, whether a service sits downstream of another, and which anomaly began first.
namespace tb {
namespace {

constexpr int64_t kSec = 1000000;

bool is_fail(const Span& s) { return s.kind == "server" ? s.status >= 500 : (s.status <= 0 || s.status >= 500); }

// Per failed entry request, the services where its failure originates: the failing leaves of its call tree
// (a failing span none of whose children fail). A failed call to a callee originates at the callee.
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

bool specific(const json& h) {
  static const std::set<std::string> c = {"process_crash", "pool_exhaustion", "capacity_saturation", "deploy_regression", "config_regression", "retry_amplification"};
  if (!c.count(h["category"].get<std::string>())) return false;
  if (h["category"] == "capacity_saturation") {  // only a real drop in the worker limit is a cause; queueing alone is as likely an effect
    const auto& f = h["features"];
    return f.value("workers_nominal", 0) > 0 && f.value("workers_min", 0) * 2 <= f.value("workers_nominal", 0);
  }
  return true;
}

}  // namespace

json analyze_v2(Incident& inc) {
  json rep = analyze(inc);  // v1, unchanged; leaves `inc` on the aligned clock
  Correlated cor = correlate(inc);
  int n_failed = 0;
  const auto share = origin_share(inc, cor, n_failed);
  double best_share = 0;
  for (auto& [k, v] : share) best_share = std::max(best_share, v);

  // downstream relation from the observed call graph
  std::map<std::string, std::set<std::string>> calls;
  for (auto& e : rep["graph"]) calls[e["caller"].get<std::string>()].insert(e["callee"].get<std::string>());
  auto downstream = [&](const std::string& x, const std::string& y) {  // does x call y, directly or transitively?
    std::set<std::string> seen;
    std::vector<std::string> st = {x};
    while (!st.empty()) {
      auto a = st.back();
      st.pop_back();
      for (auto& b : calls[a]) if (seen.insert(b).second) { if (b == y) return true; st.push_back(b); }
    }
    return false;
  };

  json& hs = rep["hypotheses"];
  std::vector<json> v(hs.begin(), hs.end());
  auto note = [](json& h, const std::string& s) {
    if (!h["features"].contains("v2_adjustments")) h["features"]["v2_adjustments"] = json::array();
    h["features"]["v2_adjustments"].push_back(s);
  };
  auto pct = [](double x) { return std::to_string(static_cast<int>(std::lround(x * 100))) + "%"; };

  // F1: a symptom-level hypothesis must originate a real share of the failed requests.
  if (n_failed >= 20 && best_share >= 0.5)
    for (auto& h : v) {
      const std::string cat = h["category"].get<std::string>(), svc = h["service"].get<std::string>();
      if (cat != "deploy_regression" && cat != "internal_errors" && cat != "dependency_slow") continue;
      const double s = share.count(svc) ? share.at(svc) : 0.0;
      h["features"]["failed_requests_originating_here"] = std::round(s * 1000) / 1000;
      if (s < 0.10) {
        h["strength"] = h["strength"].get<double>() * 0.3;
        note(h, "demoted: only " + pct(s) + " of failed requests originate in " + svc + ", while another service originates " + pct(best_share));
      }
    }
  // F6: a generic symptom upstream is an effect of a specific cause that started no later on a service it calls.
  for (auto& h : v) {
    const std::string cat = h["category"].get<std::string>();
    if (cat != "internal_errors" && cat != "edge_unreachable" && cat != "dependency_slow") continue;
    const std::string x = h["service"].get<std::string>();
    for (auto& s : v) {
      if (!specific(s) || s["strength"].get<double>() < 0.6) continue;
      const std::string y = s["service"].get<std::string>();
      if (y == x || !downstream(x, y) || s["onset_us"].get<int64_t>() > h["onset_us"].get<int64_t>() + 2 * kSec) continue;
      h["strength"] = h["strength"].get<double>() * 0.3;
      note(h, "demoted: " + s["category"].get<std::string>() + " in " + y + ", which " + x + " calls, began no later");
      break;
    }
  }

  for (auto& h : v) {
    h["score"] = h["strength"].get<double>() * (0.6 + 0.4 * h["coverage"].get<double>());
  }
  std::stable_sort(v.begin(), v.end(), [](const json& a, const json& b) { return a["score"].get<double>() > b["score"].get<double>(); });
  int rank = 0;
  for (auto& h : v) {
    h["rank"] = ++rank;
    h["hyp_id"] = "H" + std::to_string(rank);
    h["strength"] = std::round(h["strength"].get<double>() * 1000) / 1000;
    h["score"] = std::round(h["score"].get<double>() * 1000) / 1000;
  }
  if (!v.empty()) {
    const double top = v[0]["score"].get<double>(), second = v.size() > 1 ? v[1]["score"].get<double>() : 0.0;
    rep["confidence"] = (top >= 0.65 && top - second >= 0.2) ? "high" : top >= 0.45 ? "medium" : "low";
  }
  hs = json(v);
  rep["analyzer_version"] = "v2";
  return rep;
}

}  // namespace tb

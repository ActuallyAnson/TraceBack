#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "tb/analyzer_v5.hpp"

// O-rule (origin candidate). Failures have an origin: a failing span none of whose children fail (a failed call to a callee originates at the callee).
// If the analyzer's top candidate is not where failing leaves are, and a service that no existing candidate names owns a real share of them, that service
// is added as a candidate and ranked first. Nothing is keyed on service names, flags or incident ids.
namespace tb {
namespace {

constexpr int kMinLeaves = 5;          // a service needs at least this many excess failing leaf spans
constexpr double kMinShare = 0.2;      // ... and this share of all excess failing leaf spans

bool is_fail(const Span& s) { return s.kind == "server" ? s.status >= 500 : (s.status <= 0 || s.status >= 500); }

}  // namespace

json analyze_v5(Incident& inc, const std::string& dir) {
  json rep = analyze_v4(inc, dir);  // leaves `inc` on the aligned clock with normalised statuses
  const Correlated cor = correlate(inc);
  int64_t w0 = INT64_MAX, w1 = INT64_MIN;
  for (auto& s : inc.spans) { w0 = std::min(w0, s.t); w1 = std::max(w1, s.t); }
  const int64_t split = w0 + (w1 - w0) / 4;  // the first quarter of the window is the quiet baseline; failing leaves there are background noise
  std::map<std::string, std::vector<int>> leaves;  // origin service -> failing leaf spans after the baseline period
  std::map<std::string, int> base;                 // ... and inside it
  for (size_t i = 0; i < inc.spans.size(); ++i) {
    if (!is_fail(inc.spans[i])) continue;
    bool child_fail = false;
    for (int c : cor.children[i]) if (is_fail(inc.spans[c])) child_fail = true;
    if (child_fail) continue;
    const Span& s = inc.spans[i];
    const std::string o = s.kind == "server" || s.peer.empty() ? s.service : s.peer;
    if (s.t < split) ++base[o]; else leaves[o].push_back(static_cast<int>(i));
  }
  // excess = failing leaves after the baseline minus what the baseline rate predicts for the same duration
  std::map<std::string, double> excess;
  double total = 0;
  for (auto& [svc, idx] : leaves) {
    const double expected = base.count(svc) ? base[svc] * 3.0 : 0.0;  // the later part is three times as long as the baseline quarter
    excess[svc] = std::max(0.0, static_cast<double>(idx.size()) - expected);
    total += excess[svc];
  }
  rep["analyzer_version"] = "v5";
  json& hs = rep["hypotheses"];
  if (total <= 0 || hs.empty()) return rep;
  // the service that owns the largest qualifying share of the excess failing leaves
  std::string best;
  for (auto& [svc, e] : excess)
    if (e >= kMinLeaves && e / total >= kMinShare && (best.empty() || e > excess[best])) best = svc;
  if (best.empty() || hs[0]["service"].get<std::string>() == best) return rep;
  // Only symptom-level top candidates give way. A specific cause (deploy, config, crash, pool, capacity) usually sits upstream of where the failing leaves are.
  // A "retry storm" whose attempts overlap in time (negative median gap) is not retrying, it is concurrent identical calls: also symptom-level.
  static const std::set<std::string> generic = {"dependency_slow", "internal_errors", "edge_unreachable"};
  const std::string top_cat = hs[0]["category"].get<std::string>();
  const bool overlapping = top_cat == "retry_amplification" && hs[0]["features"].value("median_gap_between_attempts_ms", 0.0) < 0;
  if (!generic.count(top_cat) && !overlapping) return rep;
  const double share = excess[best] / total;
  std::vector<int>& idx = leaves[best];
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return inc.spans[a].t < inc.spans[b].t; });
  std::vector<json> v(hs.begin(), hs.end());
  int at = -1;
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i]["service"] == best && (at < 0 || v[i]["score"].get<double>() > v[at]["score"].get<double>())) at = static_cast<int>(i);
  const double floor = v[0]["score"].get<double>();
  json h;
  if (at >= 0) {
    h = v[at];
    v.erase(v.begin() + at);
    if (!h["features"].contains("v5_adjustments")) h["features"]["v5_adjustments"] = json::array();
    h["features"]["v5_adjustments"].push_back("promoted: " + best + " owns " + std::to_string(static_cast<int>(std::lround(share * 100))) + "% of the failing leaf spans beyond the baseline noise");
  } else {
    json ev = json::array(), sup = json::array();
    std::set<int> traces;
    for (size_t k = 0; k < idx.size(); ++k) {
      if (k < 8) ev.push_back({{"id", inc.spans[idx[k]].id}, {"note", "failing leaf span originating in " + best + " (status " + std::to_string(inc.spans[idx[k]].status) + ")"}});
      if (k < 40) sup.push_back(inc.spans[idx[k]].id);
      traces.insert(cor.trace_of.at(inc.spans[idx[k]].trace));
    }
    int failed_hit = 0, failed_all = 0;
    for (size_t t = 0; t < cor.traces.size(); ++t) if (cor.traces[t].failed) { ++failed_all; if (traces.count(static_cast<int>(t))) ++failed_hit; }
    int64_t t0 = inc.spans[idx.front()].t, t1 = t0;
    for (int i : idx) t1 = std::max(t1, inc.spans[i].t + inc.spans[i].dur);
    h = {{"category", "internal_errors"}, {"service", best}, {"edge", nullptr}, {"onset_us", t0}, {"end_us", t1}, {"strength", std::min(0.95, 0.5 + 0.5 * share)}, {"coverage", failed_all ? static_cast<double>(failed_hit) / failed_all : 0.0},
         {"summary", best + " is where " + std::to_string(static_cast<int>(std::lround(excess[best]))) + " failing leaf spans beyond the baseline noise originate (" + std::to_string(static_cast<int>(std::lround(share * 100))) + "% of all)"},
         {"features", {{"v5_origin_excess_leaves", std::round(excess[best])}, {"v5_origin_share", std::round(share * 1000) / 1000}}}, {"evidence", ev}, {"support_ids", sup}};
  }
  h["score"] = std::max(h["strength"].get<double>() * (0.6 + 0.4 * h["coverage"].get<double>()), floor + 0.001);  // ranked first by construction
  v.insert(v.begin(), h);
  int rank = 0;
  for (auto& x : v) {
    x["rank"] = ++rank;
    x["hyp_id"] = "H" + std::to_string(rank);
    x["strength"] = std::round(x["strength"].get<double>() * 1000) / 1000;
    x["score"] = std::round(x["score"].get<double>() * 1000) / 1000;
    x["coverage"] = std::round(x["coverage"].get<double>() * 1000) / 1000;
  }
  const double top = v[0]["score"].get<double>(), second = v.size() > 1 ? v[1]["score"].get<double>() : 0.0;
  rep["confidence"] = (top >= 0.65 && top - second >= 0.2) ? "high" : top >= 0.45 ? "medium" : "low";
  hs = json(v);
  rep["symptom"]["origin_candidate_promoted"] = best;
  return rep;
}

}  // namespace tb

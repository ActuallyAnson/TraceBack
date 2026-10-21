#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "tb/analyzer_v6.hpp"

// L-rule (log evidence). A service that logs a burst of warnings or errors beyond its own baseline is evidence about that service; a caller that logs
// because a service it calls is logging too is a symptom, not a cause. Nothing is keyed on service names, message text, flags or incident ids.
namespace tb {
namespace {
constexpr int kMinExcess = 3;  // excess warn/error/fatal logs (after the baseline quarter, minus the baseline count tripled)
}

namespace {
void rerank(json& rep, std::vector<json>& v) {
  std::stable_sort(v.begin(), v.end(), [](const json& a, const json& b) { return a["score"].get<double>() > b["score"].get<double>(); });
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
  rep["hypotheses"] = json(v);
}
}  // namespace

json analyze_v6(Incident& inc, const std::string& dir) {
  json rep = analyze_v5(inc, dir);  // leaves `inc` on the aligned clock
  rep["analyzer_version"] = "v6";
  if (rep["symptom"].contains("origin_candidate_promoted")) return rep;  // v5's measured failure origin stands
  // R-rule: a "retry storm" whose attempts overlap in time (negative median gap) is concurrent identical calls, not retries: demoted like any unsupported symptom.
  {
    std::vector<json> v(rep["hypotheses"].begin(), rep["hypotheses"].end());
    bool demoted = false;
    for (auto& h : v)
      if (h["category"] == "retry_amplification" && h["features"].value("median_gap_between_attempts_ms", 0.0) < 0) {
        h["strength"] = h["strength"].get<double>() * 0.3;
        h["score"] = h["strength"].get<double>() * (0.6 + 0.4 * h["coverage"].get<double>());
        if (!h["features"].contains("v6_adjustments")) h["features"]["v6_adjustments"] = json::array();
        h["features"]["v6_adjustments"].push_back("demoted: attempts overlap in time, so these are concurrent calls, not retries");
        demoted = true;
      }
    if (demoted) rerank(rep, v);
  }
  json& hs = rep["hypotheses"];
  if (hs.empty()) return rep;
  static const std::set<std::string> generic = {"dependency_slow", "internal_errors", "edge_unreachable"};
  const std::string top_cat = hs[0]["category"].get<std::string>();
  const bool overlapping = top_cat == "retry_amplification" && hs[0]["features"].value("median_gap_between_attempts_ms", 0.0) < 0;
  if (!generic.count(top_cat) && !overlapping) return rep;  // a specific cause (deploy, config, crash, pool, capacity) stands
  int64_t w0 = INT64_MAX, w1 = INT64_MIN;
  for (auto& s : inc.spans) { w0 = std::min(w0, s.t); w1 = std::max(w1, s.t); }
  const int64_t split = w0 + (w1 - w0) / 4;
  const std::set<std::string> known(inc.services.begin(), inc.services.end());
  std::map<std::string, int> base, after;
  std::map<std::string, std::vector<const Log*>> lines;
  for (auto& l : inc.logs) {
    if (!(l.level == "warn" || l.level == "error" || l.level == "fatal") || !known.count(l.service)) continue;
    if (l.t < split) ++base[l.service]; else { ++after[l.service]; lines[l.service].push_back(&l); }
  }
  std::map<std::string, int> excess;
  for (auto& [svc, n] : after) if (n - 3 * base[svc] >= kMinExcess) excess[svc] = n - 3 * base[svc];
  if (excess.empty()) return rep;
  std::map<std::string, std::set<std::string>> calls;
  for (auto& e : rep["graph"]) calls[e["caller"].get<std::string>()].insert(e["callee"].get<std::string>());
  auto reaches = [&](const std::string& x, const std::string& y) {
    std::set<std::string> seen;
    std::vector<std::string> st = {x};
    while (!st.empty()) {
      const std::string a = st.back();
      st.pop_back();
      for (auto& b : calls[a]) if (seen.insert(b).second) { if (b == y) return true; st.push_back(b); }
    }
    return false;
  };
  std::string best;
  int total = 0;
  for (auto& [svc, e] : excess) {
    bool symptom = false;
    for (auto& [other, e2] : excess) if (other != svc && reaches(svc, other)) symptom = true;
    if (symptom) continue;
    total += e;
    if (best.empty() || e > excess[best]) best = svc;
  }
  if (best.empty() || hs[0]["service"].get<std::string>() == best) return rep;
  const double share = static_cast<double>(excess[best]) / total;
  std::vector<json> v(hs.begin(), hs.end());
  int at = -1;
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i]["service"] == best && (at < 0 || v[i]["score"].get<double>() > v[at]["score"].get<double>())) at = static_cast<int>(i);
  const double floor = v[0]["score"].get<double>();
  json h;
  const std::string why = std::to_string(excess[best]) + " warning/error logs from " + best + " beyond its baseline, and no service it calls logs more";
  if (at >= 0) {
    h = v[at];
    v.erase(v.begin() + at);
    if (!h["features"].contains("v6_adjustments")) h["features"]["v6_adjustments"] = json::array();
    h["features"]["v6_adjustments"].push_back("promoted: " + why);
  } else {
    json ev = json::array(), sup = json::array();
    auto& ls = lines[best];
    std::sort(ls.begin(), ls.end(), [](const Log* a, const Log* b) { return a->t < b->t; });
    for (size_t k = 0; k < ls.size(); ++k) {
      if (k < 8) ev.push_back({{"id", ls[k]->id}, {"note", ls[k]->level + " log from " + best + ": " + ls[k]->msg.substr(0, 80)}});
      if (k < 40) sup.push_back(ls[k]->id);
    }
    h = {{"category", "internal_errors"}, {"service", best}, {"edge", nullptr}, {"onset_us", ls.front()->t}, {"end_us", ls.back()->t}, {"strength", std::min(0.9, 0.5 + 0.4 * share)}, {"coverage", 0.0},
         {"summary", why}, {"features", {{"v6_excess_logs", excess[best]}, {"v6_share", std::round(share * 1000) / 1000}}}, {"evidence", ev}, {"support_ids", sup}};
  }
  h["score"] = std::max(h["strength"].get<double>() * (0.6 + 0.4 * h["coverage"].get<double>()), floor + 0.001);
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
  rep["symptom"]["log_candidate_promoted"] = best;
  return rep;
}

}  // namespace tb

#include "tb/hybrid2.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "tb/analyzer_v6.hpp"

namespace tb {
namespace {
constexpr int kMinLogs = 3;  // excess warning/error/fatal log lines for a service to count as measured anomalous
std::string sget(const json& j, const char* k) { return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }
}  // namespace

json hybrid2(const std::string& dir, const ReasonOptions& bopts) {
  ReasonOptions bo = bopts;
  bo.system = "baseline";
  Incident inc_b = load_incident(dir);
  const json base = reason(inc_b, bo);  // the baseline's own request: cached when it was run before
  const json& ans = base["answer"];

  Incident inc = load_incident(dir);
  json rep = analyze_v6(inc, dir);
  const std::set<std::string> valid = inc.all_ids();
  json rejected = json::array();
  std::vector<HybridCand> props = propose_from_answer(ans, valid, inc.services, rejected);

  // measured evidence 1: warning/error log bursts beyond the service's own baseline (first quarter of the window)
  const int64_t split = inc.win_start + (inc.win_end - inc.win_start) / 4;
  std::map<std::string, int> before, after;
  const std::set<std::string> known(inc.services.begin(), inc.services.end());
  for (auto& l : inc.logs) {
    if (!(l.level == "warn" || l.level == "error" || l.level == "fatal") || !known.count(l.service)) continue;
    (l.ts < split ? before : after)[l.service]++;
  }
  std::map<std::string, int> excess;
  for (auto& [svc, n] : after) if (n - 3 * before[svc] >= kMinLogs) excess[svc] = n - 3 * before[svc];
  // measured evidence 2: the analyzer's measured failure origin (failing leaf spans beyond the baseline noise), when there are spans
  std::string origin = rep["symptom"].value("origin_candidate_promoted", std::string());
  std::set<std::string> supported;
  for (auto& [svc, e] : excess) supported.insert(svc);
  if (!origin.empty()) supported.insert(origin);
  // symptoms: a supported service that (transitively) calls another supported service is an effect, not a cause
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
  std::set<std::string> causes;
  for (auto& s : supported) {
    bool sym = false;
    for (auto& o : supported) if (o != s && reaches(s, o)) sym = true;
    if (!sym) causes.insert(s);
  }
  int total = 0;
  for (auto& s : causes) total += excess.count(s) ? excess[s] : 0;

  json out = {{"incident_id", inc.id}, {"system", "hybrid2"}, {"meta", base["meta"]}, {"accepted", true}};
  json cj = json::array();
  for (auto& p : props) cj.push_back({{"category", p.category}, {"service", p.service}, {"llm_top", p.llm_top}, {"supported", supported.count(p.service) > 0}, {"cause", causes.count(p.service) > 0}, {"excess_logs", excess.count(p.service) ? excess[p.service] : 0}});
  json hy = {{"proposals", cj}, {"rejected", rejected}, {"supported", supported}, {"causes", causes}, {"origin", origin}};
  auto answer = [&](const std::string& cat, const std::string& svc, const std::string& conf, const std::string& text, const std::vector<std::string>& cites, const std::string& source) {
    out["answer"] = {{"root_cause", {{"category", cat}, {"service", svc}, {"summary", text}}}, {"confidence", conf}, {"causal_chain", json::array({{{"claim", text}, {"status", "uncertain"}, {"cites", cites}}})}, {"alternatives", json::array()}};
    hy["source"] = source;
  };
  // choice: the model's first proposal that the measurements support and that is not a symptom
  const HybridCand* pick = nullptr;
  for (auto& p : props) if (causes.count(p.service)) { pick = &p; break; }
  std::string model_conf = ans.is_object() ? sget(ans, "confidence") : "low";
  if (pick) {
    const double share = total ? static_cast<double>(excess.count(pick->service) ? excess[pick->service] : 0) / total : 0.0;
    std::string ceiling = (pick->service == origin || share >= 0.5) ? "high" : "medium";
    std::string conf = pick->llm_top ? min_conf(model_conf.empty() ? "low" : model_conf, ceiling) : min_conf("medium", ceiling);
    answer(pick->category, pick->service, conf, pick->text, pick->cites, pick->llm_top ? "model top, measured" : "model alternative, measured");
  } else if (!causes.empty()) {
    std::string best;
    for (auto& s : causes) if (best.empty() || (excess.count(s) ? excess[s] : 0) > (excess.count(best) ? excess[best] : 0) || s == origin) best = s;
    std::vector<std::string> cites;
    for (auto& l : inc.logs) if (l.service == best && (l.level == "warn" || l.level == "error" || l.level == "fatal") && l.ts >= split && cites.size() < 5) cites.push_back(l.id);
    answer("internal_errors", best, "medium", "no model proposal is supported by the measurements; " + best + " shows the measured anomaly", cites, "measurement only");
  } else {
    answer("other", "none", "low", "no measured anomaly in the window", {}, "none");
    out["answer"]["root_cause"]["service"] = "none";
  }
  out["hybrid"] = hy;
  return out;
}

}  // namespace tb

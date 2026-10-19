#include "tb/verify.hpp"

#include <algorithm>
#include <fstream>
#include <set>

#include "../sim/sim.hpp"

namespace tb {

std::vector<std::string> classes_for_category(const std::string& c) {
  if (c == "dependency_slow") return {"slowdown"};
  if (c == "retry_amplification") return {"retry_policy"};
  if (c == "pool_exhaustion") return {"db_pool"};
  if (c == "deploy_regression") return {"deploy"};
  if (c == "config_regression") return {"config_change"};
  if (c == "process_crash") return {"restart"};
  if (c == "capacity_saturation") return {"resource_limit"};
  if (c == "internal_errors") return {"errors"};
  if (c == "edge_unreachable") return {"network"};
  return {};  // "other": nothing to switch off
}

std::string apply_verification(const std::string& conf, const std::string& status) {
  if (status == "failed") return "low";
  if (status == "inconclusive" && conf == "high") return "medium";
  return conf;
}

double observed_peak_failure_rate(const Incident& inc) {
  std::vector<std::pair<int64_t, bool>> roots;  // (aligned start, failed)
  for (auto& s : inc.spans) if (s.kind == "server" && s.parent.empty()) roots.push_back({s.t, s.status >= 500 || s.status <= 0});
  if (roots.empty()) return 0;
  std::sort(roots.begin(), roots.end());
  const int64_t lo = roots.front().first, hi = roots.back().first, w = 30000000;
  double peak = 0;
  for (int64_t a = lo; a + w <= hi + 5000000; a += 5000000) {
    int n = 0, e = 0;
    for (auto& [t, f] : roots) if (t >= a && t < a + w) { ++n; e += f; }
    if (n >= 100) peak = std::max(peak, static_cast<double>(e) / n);
  }
  return peak;
}

VerifyResult verify_hypothesis(const std::string& path, double observed, const std::string& category, const std::string& service) {
  VerifyResult r;
  r.rate_observed = observed;
  std::ifstream in(path);
  if (!in) { r.status = "inconclusive"; r.reason = "no replay manifest available"; return r; }
  json j = json::parse(in);
  sim::ReplayEnv env;
  env.id = j.value("id", "");
  env.seed = static_cast<uint64_t>(j.value("seed", int64_t{1}));
  env.epoch_us = j.value("epoch_us", int64_t{0});
  for (int i = 0; i < sim::NSVC && i < static_cast<int>(j["skew_s"].size()); ++i) env.skew_s[i] = j["skew_s"][i];
  std::vector<std::string> klass, svc_name;
  std::vector<int> edge_other;  // second endpoint for edge conditions (or -1)
  for (auto& c : j["conditions"]) {
    env.conditions.push_back({c.value("kind", 0), c.value("svc", 0), c.value("t0_us", int64_t{0}), c.value("t1_us", int64_t{0}), c.value("p1", 0.0), c.value("p2", 0.0), c.value("i1", 0)});
    klass.push_back(c.value("class", ""));
    svc_name.push_back(c.value("service", ""));
    edge_other.push_back(c["class"] == "network" ? c.value("i1", -1) : -1);
  }
  std::set<std::string> want;
  for (auto& c : classes_for_category(category)) want.insert(c);
  std::vector<bool> all(env.conditions.size(), true), off = all;
  std::vector<size_t> matched;
  for (size_t i = 0; i < env.conditions.size(); ++i) {
    if (!want.count(klass[i])) continue;
    const bool svc_ok = svc_name[i] == service || (edge_other[i] >= 0 && edge_other[i] < sim::NSVC && std::string(sim::kSvcName[edge_other[i]]) == service);
    if (svc_ok) { matched.push_back(i); off[i] = false; r.matched.push_back(klass[i] + "@" + svc_name[i]); }
  }
  if (matched.empty()) { r.status = "inconclusive"; r.reason = "no replayable condition matches this hypothesis"; return r; }
  r.rate_all = sim::replay_error_rate(env, all);
  if (r.rate_all < 0.05 || r.rate_all < 0.5 * observed) {
    r.status = "inconclusive";
    r.reason = "the replay with everything enabled does not reproduce the incident";
    return r;
  }
  r.rate_disabled = sim::replay_error_rate(env, off);
  const bool cleared = r.rate_disabled <= 0.04 && r.rate_disabled <= 0.3 * r.rate_all;
  r.status = cleared ? "passed" : "failed";
  r.reason = cleared ? "disabling the matching condition(s) makes the incident disappear" : "the incident persists with the matching condition(s) disabled";
  return r;
}

}  // namespace tb

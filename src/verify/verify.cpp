#include "tb/verify.hpp"

#include <algorithm>
#include <fstream>
#include <set>

#include "../sim/sim.hpp"
#include "../sim2/fanout.hpp"

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
  const bool fan = j.value("topology", "") == "fanout";  // second topology (fan-out with a broker)
  std::vector<sim::Condition> conds;
  std::vector<std::string> klass, svc_name, peer_name;
  for (auto& c : j["conditions"]) {
    conds.push_back({c.value("kind", 0), c.value("svc", 0), c.value("t0_us", int64_t{0}), c.value("t1_us", int64_t{0}), c.value("p1", 0.0), c.value("p2", 0.0), c.value("i1", 0)});
    klass.push_back(c.value("class", ""));
    svc_name.push_back(c.value("service", ""));
    std::string peer;
    if (fan) peer = c.value("peer", "");
    else if (c["class"] == "network") { const int i1 = c.value("i1", -1); if (i1 >= 0 && i1 < sim::NSVC) peer = sim::kSvcName[i1]; }
    peer_name.push_back(peer);
  }
  sim::ReplayEnv env;
  sim2::ReplayEnv env2;
  env.id = env2.id = j.value("id", "");
  env.seed = env2.seed = static_cast<uint64_t>(j.value("seed", int64_t{1}));
  env.epoch_us = env2.epoch_us = j.value("epoch_us", int64_t{0});
  for (int i = 0; i < static_cast<int>(j["skew_s"].size()); ++i) { if (i < sim::NSVC) env.skew_s[i] = j["skew_s"][i]; if (i < sim2::NSVC) env2.skew_s[i] = j["skew_s"][i]; }
  env.conditions = env2.conditions = conds;
  std::set<std::string> want;
  for (auto& c : classes_for_category(category)) want.insert(c);
  std::vector<bool> all(conds.size(), true), off = all;
  std::vector<size_t> matched;
  for (size_t i = 0; i < conds.size(); ++i) {
    if (!want.count(klass[i])) continue;
    const bool svc_ok = svc_name[i] == service || (!peer_name[i].empty() && peer_name[i] == service);
    if (svc_ok) { matched.push_back(i); off[i] = false; r.matched.push_back(klass[i] + "@" + svc_name[i]); }
  }
  if (matched.empty()) { r.status = "inconclusive"; r.reason = "no replayable condition matches this hypothesis"; return r; }
  r.rate_all = fan ? sim2::replay_error_rate(env2, all) : sim::replay_error_rate(env, all);
  if (r.rate_all < 0.05 || r.rate_all < 0.5 * observed) {
    r.status = "inconclusive";
    r.reason = "the replay with everything enabled does not reproduce the incident";
    return r;
  }
  r.rate_disabled = fan ? sim2::replay_error_rate(env2, off) : sim::replay_error_rate(env, off);
  r.series_all = fan ? sim2::replay_failure_series(env2, all) : sim::replay_failure_series(env, all);
  r.series_disabled = fan ? sim2::replay_failure_series(env2, off) : sim::replay_failure_series(env, off);
  const bool cleared = r.rate_disabled <= 0.04 && r.rate_disabled <= 0.3 * r.rate_all;
  r.status = cleared ? "passed" : "failed";
  r.reason = cleared ? "disabling the matching condition(s) makes the incident disappear" : "the incident persists with the matching condition(s) disabled";
  return r;
}

}  // namespace tb

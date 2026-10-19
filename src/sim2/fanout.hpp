#pragma once
// Second simulated topology: an event-driven fan-out with a message broker.
//   client -> api -> ingest -> broker --+--> billing   (awaited, parallel, DB pool)
//                                       +--> shipping  (awaited, parallel)
//                                       +--> notifier  (fire-and-forget)
// Separate implementation from src/sim (topology 1); reuses its Fault/Output types and fault vocabulary.
#include <string>
#include <vector>

#include "../sim/sim.hpp"

namespace tb::sim2 {

enum Svc { API, INGEST, BROKER, BILLING, SHIPPING, NOTIFIER, NSVC };
extern const char* const kSvcName[NSVC];

struct Spec {
  std::string id, group, split;
  uint64_t seed = 1;
  int64_t epoch_us = 0;
  std::vector<sim::Fault> faults;  // faults[0] is the true cause; the rest are decoys (Fault::svc indexes sim2::Svc)
  double skew_s[NSVC] = {0, 0, 0, 0, 0, 0};
};

struct ReplayEnv {
  std::string id;
  uint64_t seed = 1;
  int64_t epoch_us = 0;
  double skew_s[NSVC] = {0, 0, 0, 0, 0, 0};
  std::vector<sim::Condition> conditions;
};

extern const char* const kDescription;  // architecture sentence supplied in meta.json

sim::Output simulate(const Spec& spec, bool drop_true_cause = false);
std::string label_json(const Spec& spec, const sim::Output& real, const sim::Output& counterfactual);
std::string replay_env_json(const Spec& spec);  // includes "topology":"fanout" and peer names for edge conditions
double replay_error_rate(const ReplayEnv& env, const std::vector<bool>& enabled);
std::vector<double> replay_failure_series(const ReplayEnv& env, const std::vector<bool>& enabled);
std::vector<Spec> build_catalog(uint64_t seed);

}  // namespace tb::sim2

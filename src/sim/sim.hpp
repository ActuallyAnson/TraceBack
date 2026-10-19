#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Discrete-event simulator of a small 4-service backend. It emits only
// structured logs, spans, metrics and ops events. The analyzer never links
// against this code and never sees the Spec or the labels.
namespace tb::sim {

enum Svc { GATEWAY, ORDERS, INVENTORY, PAYMENTS, NSVC };
extern const char* const kSvcName[NSVC];

enum class FaultKind {
  SlowDependency,
  RetryStorm,
  PoolExhaustion,
  BadDeploy,
  ConfigChange,
  CrashRestart,
  CapacityReduction,
  MaskedError,
  // Harmless conditions used as decoys in double-fault incidents.
  HarmlessDeploy,
  MildLatency,
  CosmeticErrors,
  // Variants outside the original catalog (held-out test only, group "unseen").
  NetworkPartition,     // calls on one edge silently vanish; both endpoints are healthy
  DnsFailure,           // caller cannot resolve the callee; fails before any packet is sent
  CrashLoop,            // service flaps: several short crashes
  SilentRegression,     // errors and slowdown with no deploy/config event at all
  SlowSubset,           // only a subset of requests is slow (e.g. a hot key)
  GradualDegradation,   // latency ramps up slowly instead of stepping
};
const char* kind_name(FaultKind k);

struct Fault {
  FaultKind kind;
  int svc;       // service the fault lives in
  int64_t t0;    // onset, microseconds since window start (true time)
  int64_t t1;    // end
  double p1 = 0, p2 = 0;
  int i1 = 0;
};

struct Spec {
  std::string id;
  std::string group;  // base | skew | double
  std::string split;  // dev | test
  uint64_t seed = 1;
  int64_t epoch_us = 0;
  std::vector<Fault> faults;  // faults[0] is the true cause; the rest are decoys
  double skew_s[NSVC] = {0, 0, 0, 0};  // injected clock skew, seconds
  bool has_skew = false;
};

struct Output {
  std::map<std::string, std::string> files;  // observable files only
  double err_baseline = 0, err_incident = 0;  // gateway 5xx/timeout rate
  double err_overall = 0;                     // gateway failure rate over the whole window
  double err_peak30 = 0;                      // highest gateway failure rate over any 30 s window
  std::vector<double> err_series5;            // gateway failure rate per 5 s bin (not part of the observable files)
};

constexpr int64_t kDurationUs = 120LL * 1000000;

Output simulate(const Spec& spec, bool drop_true_cause = false);
std::string label_json(const Spec& spec, const Output& real, const Output& counterfactual);
uint64_t fnv1a(const std::string& s, uint64_t h = 1469598103934665603ULL);
uint64_t content_hash(const Output& o);

// ---- replay environment (used by the verifier; derived from a Spec but hides which condition is causal) ----
struct Condition { int kind; int svc; int64_t t0, t1; double p1, p2; int i1; };  // kind is an opaque code
struct ReplayEnv {
  std::string id;
  uint64_t seed = 1;
  int64_t epoch_us = 0;
  double skew_s[NSVC] = {0, 0, 0, 0};
  std::vector<Condition> conditions;
};
const char* condition_class(int kind);  // coarse class shared by harmful and harmless variants
std::string replay_env_json(const Spec& spec);  // conditions shuffled by a hash of the id
double replay_error_rate(const ReplayEnv& env, const std::vector<bool>& enabled);  // peak 30 s gateway failure rate
std::vector<double> replay_failure_series(const ReplayEnv& env, const std::vector<bool>& enabled);  // per-5 s failure rate

std::vector<Spec> build_catalog(uint64_t master_seed);
// Unseen-variant incidents (test split only), ids continue after the base catalog. Uses its own RNG
// so adding or changing them cannot move any base incident.
std::vector<Spec> build_variants(uint64_t seed, size_t first_index);

}  // namespace tb::sim

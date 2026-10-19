#pragma once
// Counterfactual verification: replay the incident with the conditions behind a hypothesis switched off and
// check whether the symptom disappears. Reads only a replay manifest (shuffled conditions with coarse classes);
// the manifest does not say which condition is causal.
#include <string>
#include <vector>

#include "analyzer.hpp"

namespace tb {

struct VerifyResult {
  std::string status;  // "passed" | "failed" | "inconclusive"
  std::string reason;
  std::vector<std::string> matched;  // "class@service" conditions that were disabled
  double rate_observed = 0, rate_all = 0, rate_disabled = 0;
  std::vector<double> series_all, series_disabled;  // per-5 s failure rate of the two replays (empty if not replayed)
};

// Which condition classes can express a hypothesis category (the environment's controllable knobs).
std::vector<std::string> classes_for_category(const std::string& category);

// Highest failure rate of entry requests over any 30 s window, measured on the (clock-aligned) incident itself.
// Call after analyze()/apply_clocks. The replay uses the same measure.
double observed_peak_failure_rate(const Incident& inc);

// observed_rate: observed_peak_failure_rate() of the incident.
VerifyResult verify_hypothesis(const std::string& replay_manifest_path, double observed_rate, const std::string& category, const std::string& service);

// Final confidence after verification (pre-registered policy in eval/SCORING.md).
std::string apply_verification(const std::string& llm_confidence, const std::string& status);

}  // namespace tb

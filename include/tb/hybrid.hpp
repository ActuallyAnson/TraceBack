#pragma once
// Hybrid investigation (eval/real3/PLAN.md): the LLM reads the raw telemetry and proposes candidate root causes, each citing
// record ids; the deterministic layer validates the citations, merges them with the analyzer's own candidates, scores every
// candidate's evidence the same way, and caps the confidence at what the checks support. The model never raises confidence.
#include <set>
#include <string>
#include <vector>

#include "reasoner.hpp"

namespace tb {

struct HybridCand {
  std::string category, service, text;
  std::string edge_from, edge_to;  // analyzer edge hypotheses only
  std::vector<std::string> cites;  // LLM: validated cited ids; analyzer: its evidence ids
  bool from_llm = false, from_analyzer = false, llm_top = false;
  int analyzer_rank = 0;
  double analyzer_score = 0, origin = 0, anomalous = 0, score = 0;
};

// The model's proposals: its answer's root cause plus its alternatives, first 3 distinct (category, service) pairs. A proposal is
// rejected (and listed in `rejected`) if its service is not in the incident, it cites nothing, or any cited id does not exist.
std::vector<HybridCand> propose_from_answer(const json& answer, const std::set<std::string>& valid_ids, const std::vector<std::string>& services, json& rejected);

// "low" < "medium" < "high"
std::string min_conf(const std::string& a, const std::string& b);
// The confidence measured evidence supports for a candidate the analyzer did not rank first: high needs >= 80% on both measures
// (origin share of the failed requests, anomalous share of its records), medium >= 50% on both, otherwise low.
std::string support_ceiling(double origin, double anomalous);

// legacy_merge reproduces the merge as it was when batch 4 was first run (a lower-ranked duplicate overwrote the analyzer rank); for the record only.
json hybrid(const std::string& incident_dir, const ReasonOptions& baseline_opts, bool legacy_merge = false);

}  // namespace tb

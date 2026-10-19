#pragma once
// LLM reasoning over an incident. Two systems share the model, the output format and the category list:
//   baseline  - rendered raw telemetry, no analysis layer
//   traceback - candidates + evidence from the deterministic analyzer
#include <string>

#include "analyzer.hpp"
#include "llm.hpp"

namespace tb {

struct ReasonOptions {
  std::string system = "traceback";  // "traceback" | "baseline"
  std::string description;          // optional architecture sentence from the incident's meta.json; replaces the prompt's first paragraph
  std::string prompt_name;           // system prompt file prefix; defaults to `system` (e.g. "baseline_plus")
  std::string prompts_dir = "prompts";
  std::string rules = "v1";          // "v1" (frozen) | "v2" (analyzer v2 + override rule; traceback only; see eval/v2/PLAN.md) | "v3" (analyzer v3 + the same override rule; eval/real2/PLAN.md)
  std::string incident_dir;          // needed by rules "v3" (optional per-span error flags)
  LlmConfig llm;
  int token_budget = 225000;  // estimated prompt tokens for the baseline; above this warn/error logs are subsampled
  bool dry_run = false;       // build the prompts, call nothing
  int max_retries = 2;        // extra attempts when output is malformed or cites ids that do not exist
  double price_in = 0.0000003, price_out = 0.0000009;  // USD per token (nvidia/nemotron-3-super-120b-a12b)
};

// Pure helpers (unit-tested).
json extract_json(const std::string& text);  // tolerant: strips fences / surrounding prose; null if none
std::string validate_answer_shape(const json& a);  // "" if OK, else a message for the model
std::vector<Claim> claims_of(const json& a);
// v2 override rule (eval/v2/PLAN.md): "" if `answer` may stand, else why it must be revised. `rep` is the analyzer report.
std::string override_problem(const Incident& inc, const json& answer, const json& rep);

// One-line rendering of a record (log/span/metric/ops) on the aligned clock; empty if the id is unknown.
std::string render_record(const Incident& inc, const std::string& id, int64_t t0_us);

// Reads the optional "description" field of <incident_dir>/meta.json (empty if absent).
std::string read_description(const std::string& incident_dir);

json reason(Incident& inc, const ReasonOptions& opt);

}  // namespace tb

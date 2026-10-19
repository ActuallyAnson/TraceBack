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
  std::string prompts_dir = "prompts";
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

json reason(Incident& inc, const ReasonOptions& opt);

}  // namespace tb

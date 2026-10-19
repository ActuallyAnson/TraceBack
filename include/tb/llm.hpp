#pragma once
// Thin client for the Nebius Token Factory OpenAI-compatible chat endpoint (libcurl).
// The API key is read from the NEBIUS_API_KEY environment variable only.
#include <string>
#include <utility>
#include <vector>

namespace tb {

struct LlmConfig {
  std::string base_url = "https://api.tokenfactory.nebius.com/v1";
  std::string model = "nvidia/nemotron-3-super-120b-a12b";
  double temperature = 0;
  int max_tokens = 16000;  // the model spends part of this on hidden reasoning tokens
  std::string cache_dir;   // if set, identical requests are served from disk (cost/latency recorded from the original call)
  int timeout_s = 600;
};

struct LlmResult {
  bool ok = false;
  std::string content, error;
  int prompt_tokens = 0, completion_tokens = 0, http_status = 0;
  double latency_s = 0;
  bool cached = false;
};

using Messages = std::vector<std::pair<std::string, std::string>>;  // (role, content)
LlmResult chat(const LlmConfig& cfg, const Messages& msgs);

}  // namespace tb

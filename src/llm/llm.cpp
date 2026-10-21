#include "tb/llm.hpp"

#include <curl/curl.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>

#include "../../third_party/nlohmann/json.hpp"

namespace tb {
using json = nlohmann::json;
namespace {

size_t sink(char* p, size_t sz, size_t n, void* out) {
  static_cast<std::string*>(out)->append(p, sz * n);
  return sz * n;
}

std::string cache_key(const LlmConfig& cfg, const json& body) {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : cfg.base_url + body.dump()) { h ^= c; h *= 1099511628211ULL; }
  char b[24];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
  return b;
}

}  // namespace

LlmResult chat(const LlmConfig& cfg, const Messages& msgs) {
  LlmResult r;
  json body = {{"model", cfg.model}, {"temperature", cfg.temperature}, {"max_tokens", cfg.max_tokens}, {"messages", json::array()}};
  for (auto& [role, content] : msgs) body["messages"].push_back({{"role", role}, {"content", content}});

  std::string cache_path;
  if (!cfg.cache_dir.empty()) {
    std::filesystem::create_directories(cfg.cache_dir);
    const std::string key = cache_key(cfg, body);
    cache_path = cfg.cache_dir + "/" + key + ".json";
    if (!cfg.fallback_cache_dir.empty() && !std::filesystem::exists(cache_path) && std::filesystem::exists(cfg.fallback_cache_dir + "/" + key + ".json"))
      std::filesystem::copy_file(cfg.fallback_cache_dir + "/" + key + ".json", cache_path);
    std::ifstream in(cache_path);
    if (in) {
      json c = json::parse(in, nullptr, false);
      if (!c.is_discarded() && c.value("ok", false)) {
        r.ok = true;
        r.content = c.value("content", "");
        r.prompt_tokens = c.value("prompt_tokens", 0);
        r.completion_tokens = c.value("completion_tokens", 0);
        r.latency_s = c.value("latency_s", 0.0);
        r.cached = true;
        return r;
      }
    }
  }

  const char* key = std::getenv("NEBIUS_API_KEY");
  if (!key || !*key) { r.error = "NEBIUS_API_KEY is not set"; return r; }

  const std::string payload = body.dump();
  for (int attempt = 0; attempt < 5; ++attempt) {
    CURL* curl = curl_easy_init();
    std::string resp;
    curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, "Content-Type: application/json");
    hdr = curl_slist_append(hdr, (std::string("Authorization: Bearer ") + key).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, (cfg.base_url + "/chat/completions").c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(cfg.timeout_s));
    const auto t0 = std::chrono::steady_clock::now();
    const CURLcode rc = curl_easy_perform(curl);
    r.latency_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);
    r.http_status = static_cast<int>(status);

    // A request that already used its whole timeout is not retried: five attempts of 600 s each hung an evaluation for over an hour when the endpoint stalled.
    const bool transient = (rc != CURLE_OK && rc != CURLE_OPERATION_TIMEDOUT) || status == 429 || status >= 500;
    if (rc == CURLE_OK && status == 200) {
      json j = json::parse(resp, nullptr, false);
      if (j.is_discarded() || !j.contains("choices") || j["choices"].empty()) { r.error = "malformed response"; return r; }
      const json& msg = j["choices"][0]["message"];
      r.content = msg.contains("content") && msg["content"].is_string() ? msg["content"].get<std::string>() : "";  // content is null when a reasoning model spends its whole budget thinking
      const std::string finish = j["choices"][0].contains("finish_reason") && j["choices"][0]["finish_reason"].is_string() ? j["choices"][0]["finish_reason"].get<std::string>() : "";
      if (j.contains("usage")) {
        r.prompt_tokens = j["usage"].value("prompt_tokens", 0);
        r.completion_tokens = j["usage"].value("completion_tokens", 0);
      }
      if (r.content.empty()) { r.error = "empty reply (finish_reason=" + finish + ")"; return r; }  // never cached, so a rerun asks again
      r.ok = true;
      if (!cache_path.empty()) {
        const std::string tmp = cache_path + ".tmp" + std::to_string(::getpid());  // write-then-rename: a concurrent reader never sees half a file
        std::ofstream(tmp) << json{{"ok", true}, {"content", r.content}, {"prompt_tokens", r.prompt_tokens}, {"completion_tokens", r.completion_tokens}, {"latency_s", r.latency_s}}.dump();
        std::filesystem::rename(tmp, cache_path);
      }
      return r;
    }
    r.error = rc != CURLE_OK ? curl_easy_strerror(rc) : "http " + std::to_string(status) + ": " + resp.substr(0, 300);
    if (!transient) return r;
    std::this_thread::sleep_for(std::chrono::seconds(2 << attempt));
  }
  return r;
}

}  // namespace tb

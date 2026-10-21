// tb_reason <incident_dir> --system traceback|baseline [--model M] [--cache DIR] [--budget TOKENS] [--prompts DIR]
// Prints the LLM answer (+ citation checks, tokens, cost, latency) as JSON. Takes only observable data.
#include <cstdio>
#include <cstring>
#include <iostream>

#include "tb/reasoner.hpp"

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: tb_reason <incident_dir> --system traceback|baseline [--model M] [--cache DIR] [--budget N] [--prompts DIR]\n"); return 2; }
  tb::ReasonOptions o;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--system") o.system = v;
    else if (k == "--model") o.llm.model = v;
    else if (k == "--cache") o.llm.cache_dir = v;
    else if (k == "--cache-fallback") o.llm.fallback_cache_dir = v;
    else if (k == "--timeout") o.llm.timeout_s = std::stoi(v);
    else if (k == "--budget") o.token_budget = std::stoi(v);
    else if (k == "--prompts") o.prompts_dir = v;
    else if (k == "--rules") o.rules = v;
    else if (k == "--prompt") o.prompt_name = v;
    else if (k == "--price-in") o.price_in = std::stod(v);
    else if (k == "--price-out") o.price_out = std::stod(v);
    else if (k == "--dry-run") o.dry_run = v == "yes";
    else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
  }
  if (o.system != "traceback" && o.system != "baseline") { std::fprintf(stderr, "--system must be traceback or baseline\n"); return 2; }
  if (o.rules != "v1" && o.rules != "v2" && o.rules != "v3" && o.rules != "v4" && o.rules != "v5" && o.rules != "v6") { std::fprintf(stderr, "--rules must be v1, v2, v3, v4, v5 or v6\n"); return 2; }
  if (o.rules != "v1" && o.system != "traceback") { std::fprintf(stderr, "--rules v2/v3/v4/v5/v6 apply to --system traceback only\n"); return 2; }
  if (o.rules != "v1" && o.prompts_dir == "prompts") o.prompts_dir = "prompts_v2";
  try {
    tb::Incident inc = tb::load_incident(argv[1]);
    o.description = tb::read_description(argv[1]);
    o.incident_dir = argv[1];
    std::cout << tb::reason(inc, o).dump(1) << "\n";
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}

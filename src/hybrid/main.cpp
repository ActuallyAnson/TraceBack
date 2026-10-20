// tb_hybrid <incident_dir> [--cache DIR] [--prompts DIR]
// LLM-first hybrid (eval/real3/PLAN.md). The model call is the raw-logs baseline's own request, so a cached baseline answer is reused.
#include <cstdio>
#include <iostream>

#include "tb/hybrid.hpp"

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: tb_hybrid <incident_dir> [--cache DIR] [--cache-fallback DIR] [--prompts DIR]\n"); return 2; }
  tb::ReasonOptions o;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--cache") o.llm.cache_dir = v;
    else if (k == "--cache-fallback") o.llm.fallback_cache_dir = v;
    else if (k == "--prompts") o.prompts_dir = v;
    else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
  }
  try {
    o.description = tb::read_description(argv[1]);
    std::cout << tb::hybrid(argv[1], o).dump(1) << "\n";
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}

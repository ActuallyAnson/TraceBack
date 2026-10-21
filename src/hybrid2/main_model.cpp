// tb_hybrid2m <incident_dir> [--cache DIR] [--model M] [--budget TOKENS] [--price-in X] [--price-out Y]
// Same as tb_hybrid2 with the model options of tb_reason (for the model-size check, eval/transfer/ultra_check.py). hybrid2.cpp is unchanged.
#include <cstdio>
#include <iostream>

#include "tb/hybrid2.hpp"

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: tb_hybrid2m <incident_dir> [options]\n"); return 2; }
  tb::ReasonOptions o;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--cache") o.llm.cache_dir = v;
    else if (k == "--model") o.llm.model = v;
    else if (k == "--budget") o.token_budget = std::stoi(v);
    else if (k == "--price-in") o.price_in = std::stod(v);
    else if (k == "--price-out") o.price_out = std::stod(v);
    else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
  }
  try {
    o.description = tb::read_description(argv[1]);
    std::cout << tb::hybrid2(argv[1], o).dump(1) << "\n";
  } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
  return 0;
}

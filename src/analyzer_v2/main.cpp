// tb_analyze2 <incident_dir>: analyzer v2 (v1 hypotheses + causal-ordering re-ranking), prints the report as JSON.
#include <cstdio>
#include <iostream>

#include "tb/analyzer_v2.hpp"

int main(int argc, char** argv) {
  if (argc != 2) { std::fprintf(stderr, "usage: tb_analyze2 <incident_dir>\n"); return 2; }
  try {
    tb::Incident inc = tb::load_incident(argv[1]);
    std::cout << tb::analyze_v2(inc).dump(1) << "\n";
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}

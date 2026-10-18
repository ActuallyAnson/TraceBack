// tb_analyze <incident_dir>: deterministic analysis, prints the report as JSON.
// Takes only an incident's observable directory; there is no labels argument.
#include <cstdio>
#include <iostream>

#include "tb/analyzer.hpp"

int main(int argc, char** argv) {
  if (argc != 2) { std::fprintf(stderr, "usage: tb_analyze <incident_dir>\n"); return 2; }
  try {
    tb::Incident inc = tb::load_incident(argv[1]);
    std::cout << tb::analyze(inc).dump(1) << "\n";
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}

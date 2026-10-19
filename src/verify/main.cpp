// tb_verify <incident_dir> --replay <manifest> --hyp category:service [--hyp ...]
#include <cstdio>
#include <iostream>

#include "tb/verify.hpp"

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: tb_verify <incident_dir> --replay <manifest> --hyp category:service ...\n"); return 2; }
  std::string replay;
  std::vector<std::string> hyps;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string k = argv[i];
    if (k == "--replay") replay = argv[i + 1];
    else if (k == "--hyp") hyps.push_back(argv[i + 1]);
  }
  try {
    tb::Incident inc = tb::load_incident(argv[1]);
    tb::json rep = tb::analyze(inc);
    const double observed = tb::observed_peak_failure_rate(inc);
    tb::json out = tb::json::array();
    for (auto& h : hyps) {
      const auto p = h.find(':');
      auto r = tb::verify_hypothesis(replay, observed, h.substr(0, p), p == std::string::npos ? "" : h.substr(p + 1));
      out.push_back({{"hypothesis", h}, {"status", r.status}, {"reason", r.reason}, {"matched", r.matched}, {"rate_observed", r.rate_observed}, {"rate_all_enabled", r.rate_all}, {"rate_disabled", r.rate_disabled}});
    }
    std::cout << out.dump(1) << "\n";
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}

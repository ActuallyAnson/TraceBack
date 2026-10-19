#include <cstdio>
#include <filesystem>
#include <fstream>

#include "../src/sim/sim.hpp"
#include "tb/verify.hpp"

using namespace tb;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const char* cat_for(sim::FaultKind k) {
  switch (k) {
    case sim::FaultKind::SlowDependency: return "dependency_slow";
    case sim::FaultKind::BadDeploy: return "deploy_regression";
    case sim::FaultKind::CrashRestart: return "process_crash";
    case sim::FaultKind::MaskedError: return "internal_errors";
    default: return "other";
  }
}

int main() {
  // confidence policy (pre-registered)
  CHECK(apply_verification("high", "passed") == "high");
  CHECK(apply_verification("medium", "passed") == "medium");
  CHECK(apply_verification("high", "failed") == "low");
  CHECK(apply_verification("high", "inconclusive") == "medium");
  CHECK(apply_verification("low", "inconclusive") == "low");
  CHECK(classes_for_category("other").empty());
  CHECK(classes_for_category("edge_unreachable") == std::vector<std::string>{"network"});

  // replay manifest hides which condition is causal: shuffled, opaque classes only
  auto cat = sim::build_catalog(20261019);
  int checked = 0;
  for (auto& spec : cat) {
    const sim::FaultKind k = spec.faults[0].kind;
    if (spec.group != "double" || std::string(cat_for(k)) == "other") continue;
    const std::string path = (std::filesystem::temp_directory_path() / "tb_test_replay.json").string();
    std::ofstream(path) << sim::replay_env_json(spec);
    sim::Output real = sim::simulate(spec, false);
    const std::string true_svc = sim::kSvcName[spec.faults[0].svc];
    VerifyResult good = verify_hypothesis(path, real.err_peak30, cat_for(k), true_svc);
    CHECK(good.status == "passed");  // disabling the true cause clears the symptom
    // the decoy's service, with the decoy's own class: symptom persists
    const auto& d = spec.faults[1];
    const char* dc = sim::condition_class(static_cast<int>(d.kind));
    for (const char* cname : {"dependency_slow", "deploy_regression", "internal_errors"}) {
      auto cls = classes_for_category(cname);
      if (cls[0] == dc && std::string(sim::kSvcName[d.svc]) != true_svc) {
        VerifyResult bad = verify_hypothesis(path, real.err_peak30, cname, sim::kSvcName[d.svc]);
        CHECK(bad.status == "failed");
      }
    }
    // a hypothesis with no replayable condition is inconclusive, never "passed"
    CHECK(verify_hypothesis(path, real.err_peak30, "other", "gateway").status == "inconclusive");
    if (++checked == 3) break;
  }
  CHECK(checked >= 2);
  std::printf(failures ? "FAILED\n" : "ok\n");
  return failures ? 1 : 0;
}

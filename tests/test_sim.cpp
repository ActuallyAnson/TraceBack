#include <cstdio>
#include <string>

#include "../src/sim/sim.hpp"

using namespace tb::sim;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  auto cat = build_catalog(20261019);
  CHECK(cat.size() == 48);
  int test = 0;
  for (auto& s : cat) test += s.split == "test";
  CHECK(test >= 15);

  // Deterministic: same spec -> byte-identical observable output.
  Output a = simulate(cat[0]), b = simulate(cat[0]);
  CHECK(content_hash(a) == content_hash(b));

  // Observable files must not leak labels (fault names, decoys, skew values).
  for (auto& [name, body] : a.files) {
    for (const char* leak : {"root_cause", "decoy", "slow_dependency", "retry_storm", "bad_deploy", "crash_restart", "capacity_reduction", "masked_downstream", "mild_latency", "cosmetic_errors", "clock_skew"})
      CHECK(body.find(leak) == std::string::npos);
  }
  // Removing the true cause changes the output.
  CHECK(content_hash(simulate(cat[0], true)) != content_hash(a));
  std::printf(failures ? "FAILED\n" : "ok\n");
  return failures ? 1 : 0;
}

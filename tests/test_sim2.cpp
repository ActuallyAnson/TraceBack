#include <cstdio>
#include <string>

#include "../src/sim2/fanout.hpp"

using namespace tb;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  auto cat = sim2::build_catalog(424242);
  CHECK(cat.size() == 14);
  for (auto& s : cat) CHECK(s.split == "test");
  // deterministic
  sim::Output a = sim2::simulate(cat[0]), b = sim2::simulate(cat[0]);
  CHECK(sim::content_hash(a) == content_hash(b));
  // the observable files carry no labels
  for (auto& [name, body] : a.files)
    for (const char* leak : {"root_cause", "decoy", "slow_dependency", "bad_deploy", "crash_restart", "capacity_reduction", "retry_storm", "masked_downstream", "network_partition", "silent_regression", "crash_loop", "clock_skew"})
      CHECK(body.find(leak) == std::string::npos);
  // six services, an entry service and an async branch show up in the data
  CHECK(a.files["meta.json"].find("notifier") != std::string::npos);
  CHECK(a.files["spans.jsonl"].find("\"peer\":\"notifier\"") != std::string::npos);
  // removing the true cause changes the output
  CHECK(sim::content_hash(sim2::simulate(cat[0], true)) != sim::content_hash(a));
  std::printf(failures ? "FAILED\n" : "ok\n");
  return failures ? 1 : 0;
}

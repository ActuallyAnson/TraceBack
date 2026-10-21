#include <cstdio>

#include "tb/reasoner.hpp"

using namespace tb;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  Incident inc;
  auto add = [&](const std::string& id, const std::string& svc, int64_t t) { Log l; l.id = id; l.service = svc; l.t = t; inc.logs.push_back(l); };
  add("pay-ok", "payments", 50000000);      // inside H1's window, about H1's service
  add("pay-early", "payments", 1000000);    // outside H1's window
  add("ord-x", "orders", 50000000);         // a different service
  add("pay-ev", "payments", 50000000);      // part of H1's own evidence
  json rep = {{"hypotheses", json::array({{{"category", "dependency_slow"}, {"service", "payments"}, {"edge", nullptr}, {"onset_us", 40000000}, {"end_us", 70000000}, {"support_ids", json::array({"pay-ev"})}}})}};
  auto ans = [](const char* cat, const char* svc) { return json{{"root_cause", {{"category", cat}, {"service", svc}}}}; };

  CHECK(override_problem(inc, ans("dependency_slow", "payments"), rep).empty());           // confirming H1 needs nothing
  CHECK(!override_problem(inc, ans("pool_exhaustion", "payments"), rep).empty());          // differing without an override field
  json a = ans("pool_exhaustion", "payments");
  a["override"] = {{"contradicting_records", json::array({"pay-ok"})}, {"why", "x"}};
  CHECK(override_problem(inc, a, rep).empty());                                            // record about H1's service, in its window, not its evidence
  a["override"]["contradicting_records"] = json::array({"pay-ev"});
  CHECK(!override_problem(inc, a, rep).empty());                                           // part of H1's own evidence
  a["override"]["contradicting_records"] = json::array({"ord-x"});
  CHECK(!override_problem(inc, a, rep).empty());                                           // about another service
  a["override"]["contradicting_records"] = json::array({"pay-early"});
  CHECK(!override_problem(inc, a, rep).empty());                                           // outside H1's window
  a["override"]["contradicting_records"] = json::array();
  CHECK(!override_problem(inc, a, rep).empty());                                           // empty list
  std::printf(failures ? "FAILED\n" : "ok\n");
  return failures ? 1 : 0;
}

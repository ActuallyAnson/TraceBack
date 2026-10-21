#include <cstdio>

#include "tb/hybrid.hpp"

using namespace tb;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  const std::set<std::string> valid = {"a1", "a2", "b1", "c1", "d1"};
  const std::vector<std::string> svcs = {"gateway", "orders", "payments"};
  json ans = {{"root_cause", {{"category", "dependency_slow"}, {"service", "payments"}, {"summary", "s"}}},
              {"causal_chain", json::array({{{"claim", "x"}, {"cites", json::array({"[a1]", "a2"})}}})},
              {"alternatives", json::array({
                  {{"category", "internal_errors"}, {"service", "orders"}, {"why_less_likely", "w"}, {"cites", json::array({"b1"})}},
                  {{"category", "pool_exhaustion"}, {"service", "orders"}, {"why_less_likely", "w"}, {"cites", json::array({"zz9"})}},   // invented id
                  {{"category", "nonsense"}, {"service", "ghost"}, {"why_less_likely", "w"}, {"cites", json::array({"c1"})}},              // unknown service
                  {{"category", "other"}, {"service", "gateway"}, {"why_less_likely", "w"}, {"cites", json::array({"d1"})}}})}};  // beyond the third proposal
  json rej = json::array();
  auto c = propose_from_answer(ans, valid, svcs, rej);
  CHECK(c.size() == 2);                                  // top + the valid alternative
  CHECK(c[0].service == "payments" && c[0].llm_top && c[0].cites.size() == 2 && c[0].cites[0] == "a1");
  CHECK(c[1].service == "orders" && !c[1].llm_top);
  CHECK(rej.size() == 1 && rej[0]["service"] == "orders" && rej[0]["invalid_ids"][0] == "zz9");  // the invented id is rejected, not repaired
  json rej2 = json::array();
  json noc = {{"root_cause", {{"category", "other"}, {"service", "orders"}}}, {"causal_chain", json::array({{{"claim", "x"}, {"cites", json::array()}}})}};
  CHECK(propose_from_answer(noc, valid, svcs, rej2).empty() && rej2.size() == 1);  // no citation, no candidate
  CHECK(propose_from_answer(json(nullptr), valid, svcs, rej2).empty());
  CHECK(min_conf("high", "medium") == "medium" && min_conf("low", "high") == "low");
  CHECK(support_ceiling(0.9, 0.9) == "high");
  CHECK(support_ceiling(0.9, 0.6) == "medium");
  CHECK(support_ceiling(0.9, 0.3) == "low");      // both measures must hold
  std::printf(failures ? "FAILED\n" : "ok\n");
  return failures ? 1 : 0;
}

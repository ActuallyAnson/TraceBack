#include <cmath>
#include <cstdio>
#include <string>

#include "tb/analyzer.hpp"

using namespace tb;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

namespace {
// Build one request: gateway -> orders -> inventory. `off` gives each service's clock offset (us);
// true network delay is 500us each way, services take 2ms/5ms/10ms.
void add_request(std::vector<Span>& out, int n, int64_t t0, std::map<std::string, int64_t> off, int root_status = 200) {
  auto tr = "trace" + std::to_string(n);
  auto mk = [&](std::string id, std::string span, std::string parent, std::string svc, std::string kind, std::string peer, int64_t start_true, int64_t dur, int status) {
    Span s;
    s.id = id; s.trace = tr; s.span = span; s.parent = parent; s.service = svc; s.kind = kind; s.peer = peer;
    s.start = start_true + off[svc]; s.dur = dur; s.status = status;
    out.push_back(s);
  };
  const std::string p = std::to_string(n);
  mk("g-S" + p, "g" + p, "", "gateway", "server", "", t0, 30000, root_status);
  mk("g-C" + p, "gc" + p, "g" + p, "gateway", "client", "orders", t0 + 2000, 26000, 200);
  mk("o-S" + p, "o" + p, "gc" + p, "orders", "server", "", t0 + 2500, 25000, 200);
  mk("o-C" + p, "oc" + p, "o" + p, "orders", "client", "inventory", t0 + 8000, 15000, 200);
  mk("i-S" + p, "i" + p, "oc" + p, "inventory", "server", "", t0 + 8500, 12000, 200);
}
}  // namespace

int main() {
  // ---- clock skew recovery from causal span pairs ----
  {
    std::vector<Span> sp;
    std::map<std::string, int64_t> off = {{"gateway", 0}, {"orders", 1500000}, {"inventory", -800000}};
    for (int i = 0; i < 60; ++i) add_request(sp, i, 1000000 + i * 40000, off);
    ClockModel cm = estimate_clocks(sp);
    CHECK(cm.reference == "gateway");
    CHECK(std::llabs(cm.offset_us["orders"] - 1500000) < 2000);
    CHECK(std::llabs(cm.offset_us["inventory"] - (-800000)) < 2000);  // chained through orders
  }
  // a skewed *reference* service: offsets are relative to it
  {
    std::vector<Span> sp;
    std::map<std::string, int64_t> off = {{"gateway", 2000000}, {"orders", 0}, {"inventory", 0}};
    for (int i = 0; i < 60; ++i) add_request(sp, i, 1000000 + i * 40000, off);
    ClockModel cm = estimate_clocks(sp);
    CHECK(std::llabs(cm.offset_us["orders"] - (-2000000)) < 2000);
  }
  // too few samples on an edge: no estimate, assume aligned
  {
    std::vector<Span> sp;
    std::map<std::string, int64_t> off = {{"gateway", 0}, {"orders", 1500000}, {"inventory", 0}};
    for (int i = 0; i < 3; ++i) add_request(sp, i, 1000000 + i * 40000, off);
    ClockModel cm = estimate_clocks(sp);
    CHECK(cm.offset_us["orders"] == 0);
  }
  // robust to outliers (queueing delays inflate server.start - client.start for some pairs)
  {
    std::vector<Span> sp;
    std::map<std::string, int64_t> off = {{"gateway", 0}, {"orders", 700000}, {"inventory", 0}};
    for (int i = 0; i < 61; ++i) add_request(sp, i, 1000000 + i * 40000, off);
    for (auto& s : sp) if (s.service == "orders" && s.kind == "server" && s.trace.size() % 2 == 0 && s.trace.back() % 5 == 0) s.start += 400000;  // 20% late arrivals
    ClockModel cm = estimate_clocks(sp);
    CHECK(std::llabs(cm.offset_us["orders"] - 700000) < 3000);
  }

  // ---- correlation: ordering under skew, parent links, lost spans ----
  {
    std::vector<Span> sp;
    std::map<std::string, int64_t> off = {{"gateway", 0}, {"orders", 3000000}, {"inventory", -2000000}};
    for (int i = 0; i < 30; ++i) add_request(sp, i, 1000000 + i * 40000, off, i == 7 ? 503 : 200);
    // lose the orders->inventory client span of request 3 (its inventory span must be re-parented)
    for (size_t i = 0; i < sp.size(); ++i) if (sp[i].id == "o-C3") { sp.erase(sp.begin() + i); break; }
    Incident inc;
    inc.spans = sp;
    ClockModel cm = estimate_clocks(inc.spans);
    apply_clocks(inc, cm);
    Correlated c = correlate(inc);
    CHECK(c.traces.size() == 30);
    // raw timestamps are misordered (orders at +3s, inventory at -2s) but aligned order is causal
    const Trace& t = c.traces[c.trace_of.at("trace5")];
    CHECK(t.spans.size() == 5);
    CHECK(inc.spans[t.spans.front()].service == "gateway");
    CHECK(inc.spans[t.spans.back()].service == "inventory");
    for (size_t i = 1; i < t.spans.size(); ++i) CHECK(inc.spans[t.spans[i - 1]].t <= inc.spans[t.spans[i]].t);
    // parents
    int g = -1, gc = -1, o = -1;
    for (int i : t.spans) { if (inc.spans[i].id == "g-S5") g = i; if (inc.spans[i].id == "g-C5") gc = i; if (inc.spans[i].id == "o-S5") o = i; }
    CHECK(c.parent[gc] == g);
    CHECK(c.parent[o] == gc);
    CHECK(t.root == g && !t.failed);
    CHECK(c.traces[c.trace_of.at("trace7")].failed);
    // lost client span: inventory span attaches to the orders server span by containment
    const Trace& t3 = c.traces[c.trace_of.at("trace3")];
    int inv = -1, ord = -1;
    for (int i : t3.spans) { if (inc.spans[i].id == "i-S3") inv = i; if (inc.spans[i].id == "o-S3") ord = i; }
    CHECK(inv >= 0 && c.parent[inv] == ord && c.virtual_parent[inv]);
    // graph
    auto edges = build_graph(inc);
    CHECK(edges.size() == 2);  // gateway->orders, orders->inventory
  }

  // ---- citation validation ----
  {
    std::set<std::string> valid = {"a-S1", "a-L2", "b-M3", "ops-E1"};
    std::set<std::string> support = {"a-S1", "a-L2"};
    std::vector<Claim> claims = {
        {"service a failed first", "confirmed", {"a-S1", "a-L2"}},
        {"deploy preceded it", "uncertain", {"ops-E1", "x-S99"}},  // x-S99 does not exist
    };
    CitationCheck r = validate_citations(claims, valid, support);
    CHECK(r.total == 4);
    CHECK(r.existing == 3);
    CHECK(r.supported == 2);
    CHECK(!r.ok());
    CHECK(r.invalid.size() == 1 && r.invalid[0] == "x-S99");
    CHECK(r.unsupported.size() == 1 && r.unsupported[0] == "ops-E1");
    CHECK(validate_citations({}, valid, support).ok());
    CHECK(validate_citations({{"t", "confirmed", {}}}, valid, support).total == 0);
  }
  std::printf(failures ? "FAILED\n" : "ok\n");
  return failures ? 1 : 0;
}

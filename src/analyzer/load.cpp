#include <fstream>
#include <stdexcept>

#include "tb/analyzer.hpp"

namespace tb {
namespace {
template <class F>
void each_line(const std::string& path, F f) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::string line;
  while (std::getline(in, line))
    if (!line.empty()) f(json::parse(line));
}
}  // namespace

std::set<std::string> Incident::all_ids() const {
  std::set<std::string> s;
  for (auto& x : logs) s.insert(x.id);
  for (auto& x : spans) s.insert(x.id);
  for (auto& x : metrics) s.insert(x.id);
  for (auto& x : ops) s.insert(x.id);
  return s;
}

Incident load_incident(const std::string& dir) {
  Incident inc;
  {
    std::ifstream in(dir + "/meta.json");
    if (!in) throw std::runtime_error("cannot open " + dir + "/meta.json");
    json m = json::parse(in);
    inc.id = m.value("id", "");
    inc.win_start = m.value("window_start_us", int64_t{0});
    inc.win_end = m.value("window_end_us", int64_t{0});
    for (auto& s : m["services"]) inc.services.push_back(s);
  }
  each_line(dir + "/logs.jsonl", [&](const json& j) {
    Log l;
    l.id = j.value("id", "");
    l.service = j.value("service", "");
    l.level = j.value("level", "");
    l.event = j.value("event", "");
    l.msg = j.value("msg", "");
    l.trace = j.value("trace_id", "");
    l.ts = j.value("ts_us", int64_t{0});
    l.f = j.contains("f") ? j["f"] : json::object();
    inc.logs.push_back(std::move(l));
  });
  each_line(dir + "/spans.jsonl", [&](const json& j) {
    Span s;
    s.id = j.value("id", "");
    s.trace = j.value("trace_id", "");
    s.span = j.value("span_id", "");
    s.parent = j.value("parent_span_id", "");
    s.service = j.value("service", "");
    s.kind = j.value("kind", "");
    s.op = j.value("op", "");
    s.peer = j.value("peer", "");
    s.error = j.value("error", "");
    s.start = j.value("start_us", int64_t{0});
    s.dur = j.value("dur_us", int64_t{0});
    s.queue = j.value("queue_us", int64_t{0});
    s.status = j.value("status", 0);
    s.attempt = j.value("attempt", 1);
    inc.spans.push_back(std::move(s));
  });
  each_line(dir + "/metrics.jsonl", [&](const json& j) {
    Metric m;
    m.id = j.value("id", "");
    m.service = j.value("service", "");
    m.ts = j.value("ts_us", int64_t{0});
    m.req = j.value("req", 0);
    m.err5xx = j.value("err5xx", 0);
    m.p50_ms = j.value("p50_ms", 0.0);
    m.p99_ms = j.value("p99_ms", 0.0);
    m.queue_max = j.value("queue_max", 0);
    m.busy_max = j.value("busy_max", 0);
    m.workers_limit = j.value("workers_limit", 0);
    m.pool_in_use_max = j.value("pool_in_use_max", 0);
    m.boots = j.value("boots", 0);
    inc.metrics.push_back(std::move(m));
  });
  each_line(dir + "/events.jsonl", [&](const json& j) {
    Ops o;
    o.id = j.value("id", "");
    o.type = j.value("type", "");
    o.service = j.value("service", "");
    o.ts = j.value("ts_us", int64_t{0});
    o.detail = j.contains("detail") ? j["detail"] : json::object();
    inc.ops.push_back(std::move(o));
  });
  return inc;
}

}  // namespace tb

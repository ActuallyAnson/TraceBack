#pragma once
// Deterministic analysis layer: load -> clock alignment -> trace correlation ->
// dependency graph -> ranked hypotheses with evidence IDs. No LLM, no labels.
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../third_party/nlohmann/json.hpp"

namespace tb {
using json = nlohmann::json;

struct Log {
  std::string id, service, level, event, msg, trace;
  json f;
  int64_t ts = 0, t = 0;  // raw timestamp, clock-aligned timestamp
};
struct Span {
  std::string id, trace, span, parent, service, kind, op, peer, error;
  int64_t start = 0, dur = 0, queue = 0, t = 0;
  int status = 0, attempt = 1;
};
struct Metric {
  std::string id, service;
  int64_t ts = 0, t = 0;
  int req = 0, err5xx = 0, queue_max = 0, busy_max = 0, workers_limit = 0, pool_in_use_max = 0, boots = 0;
  double p50_ms = 0, p99_ms = 0;
};
struct Ops {
  std::string id, type, service;
  json detail;
  int64_t ts = 0, t = 0;
};

struct Incident {
  std::string id;
  int64_t win_start = 0, win_end = 0;
  std::vector<std::string> services;
  std::vector<Log> logs;
  std::vector<Span> spans;
  std::vector<Metric> metrics;
  std::vector<Ops> ops;
  std::set<std::string> all_ids() const;
};
Incident load_incident(const std::string& dir);

// ---- clocks ----
struct ClockModel {
  std::string reference;
  std::map<std::string, int64_t> offset_us;  // service clock minus reference clock
  std::map<std::string, int> edge_samples;   // "caller->callee" -> #span pairs used
  int64_t ops_offset_us = 0;                 // ops clock minus reference clock
  bool ops_calibrated = false;
};
// Offsets come only from causal span pairs: a callee's server span starts a sub-millisecond
// network hop after its parent client span starts, so median(server.start - client.start)
// over an edge is the clock difference of that edge.
ClockModel estimate_clocks(const std::vector<Span>& spans);
void apply_clocks(Incident& inc, ClockModel& cm);

// ---- traces ----
struct Trace {
  std::string id;
  std::vector<int> spans;  // indices into Incident::spans, by aligned start time
  int root = -1;           // root server span, -1 if lost
  bool failed = false;     // root span failed (status >= 500)
};
struct Correlated {
  std::vector<Trace> traces;
  std::unordered_map<std::string, int> trace_of;     // trace id -> index
  std::vector<int> parent;                           // span index -> parent span index (or -1)
  std::vector<std::vector<int>> children;
  std::vector<bool> virtual_parent;                  // parent inferred by containment (parent span lost)
};
Correlated correlate(const Incident& inc);

struct Edge {
  std::string caller, callee;
  int calls = 0, fail = 0, timeouts = 0, refused = 0, errors = 0, retries = 0;
};
std::vector<Edge> build_graph(const Incident& inc);

// ---- hypotheses ----
json analyze(Incident& inc);  // full pipeline -> report JSON

// ---- citations ----
struct Claim {
  std::string text;
  std::string status;  // "confirmed" | "uncertain"
  std::vector<std::string> cites;
};
struct CitationCheck {
  int total = 0, existing = 0, supported = 0;
  std::vector<std::string> invalid;      // cited IDs that do not exist
  std::vector<std::string> unsupported;  // exist, but are not in the allowed evidence set
  bool ok() const { return invalid.empty(); }
};
CitationCheck validate_citations(const std::vector<Claim>& claims, const std::set<std::string>& valid_ids,
                                 const std::set<std::string>& supporting_ids);

}  // namespace tb

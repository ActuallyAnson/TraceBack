#pragma once
// Analyzer v3 = analyzer v2 plus two general fixes for telemetry with shared infrastructure services and error flags
// (eval/real2/PLAN.md). Same report format. v1 and v2 are untouched.
#include <string>

#include "analyzer_v2.hpp"

namespace tb {
// `dir` is the incident directory (spans.jsonl may carry an optional `"err":true` flag per span).
json analyze_v3(Incident& inc, const std::string& dir);
}

#pragma once
// Analyzer v4 = analyzer v3 plus one rule for telemetry where the application answers 4xx while the work beneath it failed (eval/real4/PLAN.md).
// Same report format. v1, v2 and v3 are untouched.
#include <string>

#include "analyzer_v3.hpp"

namespace tb {
json analyze_v4(Incident& inc, const std::string& dir);
}

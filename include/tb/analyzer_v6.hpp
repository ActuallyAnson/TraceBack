#pragma once
// Analyzer v6 = analyzer v5 plus a log-evidence candidate generator (eval/real6/PLAN.md). Same report format. v1 to v5 are untouched.
#include <string>

#include "analyzer_v5.hpp"

namespace tb {
json analyze_v6(Incident& inc, const std::string& dir);
}

#pragma once
// Analyzer v5 = analyzer v4 plus one candidate generator for failures that originate in a service the rule set never names (eval/real5/PLAN.md).
// Same report format. v1 to v4 are untouched.
#include <string>

#include "analyzer_v4.hpp"

namespace tb {
json analyze_v5(Incident& inc, const std::string& dir);
}

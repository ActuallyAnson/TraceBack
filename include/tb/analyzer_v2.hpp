#pragma once
// Analyzer v2 = the frozen v1 analyzer plus causal-ordering re-ranking (eval/v2/PLAN.md). Same report format.
#include "analyzer.hpp"

namespace tb {
json analyze_v2(Incident& inc);
}

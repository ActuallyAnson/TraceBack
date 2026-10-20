#pragma once
// Hybrid 2 (eval/transfer/PLAN.md): the model proposes candidates from the raw telemetry (its own answer, validated), the deterministic layer keeps only those
// that the incident's own measurements support, drops symptoms, and caps confidence at what is measured. With no measured anomaly there is no answer.
#include <string>

#include "hybrid.hpp"

namespace tb {
json hybrid2(const std::string& incident_dir, const ReasonOptions& baseline_opts);
}

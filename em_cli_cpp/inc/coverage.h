#pragma once
#include "models.h"
#include <vector>

// Direct port of the coverage/placement math from main.go. No em_cli calls
// here at all — this is pure computation over the in-memory device list,
// so it's the most mechanical part of the port.
namespace coverage {

CoverageAnalysis analyze(const CoverageRequest& request, const std::vector<Device>& devices);
std::vector<PlacementSuggestion> optimize_placement(const OptimizationRequest& request,
                                                       const std::vector<Device>& devices);
CoverageAnalysis simulate_with_placement(const std::vector<Point3D>& device_positions,
                                            const std::string& band,
                                            std::vector<Device> devices);

} // namespace coverage

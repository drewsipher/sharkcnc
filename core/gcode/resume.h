// Build a safe "resume from line N" job after an interruption: replays the
// modal setup (units, distance mode, spindle) that was in effect, lifts to a
// safe Z, then continues the remaining lines. The operator must re-home /
// re-zero to the same work origin first — this only restores program state,
// not machine position.
#pragma once
#include <string>
#include <vector>

namespace scnc {

std::vector<std::string> buildResumeJob(const std::vector<std::string>& lines,
                                        size_t fromLine, double safeZ);

}  // namespace scnc

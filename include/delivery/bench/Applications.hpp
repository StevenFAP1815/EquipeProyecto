#pragma once

#include <delivery/bench/BenchRunner.hpp>
#include <delivery/infra/Config.hpp>

#include <vector>

namespace delivery::bench {
std::vector<BenchCase> allBenchCases(const infra::Config& config);
}

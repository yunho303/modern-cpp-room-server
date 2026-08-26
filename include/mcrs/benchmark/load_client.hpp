#pragma once

#include "mcrs/benchmark/metrics.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace mcrs::benchmark
{
struct LoadTestConfig
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 7777;
    std::size_t client_count = 16;
    std::size_t warmup_moves_per_client = 20;
    std::size_t measured_moves_per_client = 200;
};

struct LoadTestResult
{
    LoadTestConfig config;
    LatencySummary latency;
    Latency elapsed;
    double commands_per_second{};
    double estimated_deliveries_per_second{};
};

[[nodiscard]] std::expected<LoadTestResult, std::string>
run_load_test(const LoadTestConfig& config);
} // namespace mcrs::benchmark

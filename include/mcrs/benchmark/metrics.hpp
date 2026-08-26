#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <span>

namespace mcrs::benchmark
{
using Latency = std::chrono::nanoseconds;

enum class MetricsError
{
    no_samples,
    negative_latency,
    non_positive_elapsed_time,
};

struct LatencySummary
{
    std::size_t sample_count{};
    Latency minimum{};
    Latency p50{};
    Latency p95{};
    Latency p99{};
    Latency maximum{};

    bool operator==(const LatencySummary&) const = default;
};

[[nodiscard]] std::expected<LatencySummary, MetricsError>
summarize_latencies(std::span<const Latency> samples);

[[nodiscard]] std::expected<double, MetricsError>
calculate_throughput(std::size_t completed_operations, Latency elapsed);
} // namespace mcrs::benchmark

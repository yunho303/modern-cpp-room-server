#include "mcrs/benchmark/metrics.hpp"

#include <algorithm>
#include <ranges>
#include <vector>

namespace mcrs::benchmark
{
namespace
{
[[nodiscard]] Latency nearest_rank_percentile(const std::vector<Latency>& sorted_samples,
                                              std::size_t percentile) noexcept
{
    const auto sample_count = sorted_samples.size();
    const auto full_hundreds = sample_count / 100U;
    const auto remainder = sample_count % 100U;
    const auto rank = full_hundreds * percentile + (remainder * percentile + 99U) / 100U;
    return sorted_samples[rank - 1U];
}
} // namespace

std::expected<LatencySummary, MetricsError>
summarize_latencies(std::span<const Latency> samples)
{
    if (samples.empty())
    {
        return std::unexpected(MetricsError::no_samples);
    }

    if (std::ranges::any_of(samples, [](Latency sample)
                            { return sample < Latency::zero(); }))
    {
        return std::unexpected(MetricsError::negative_latency);
    }

    std::vector<Latency> sorted_samples{samples.begin(), samples.end()};
    std::ranges::sort(sorted_samples);

    return LatencySummary{
        .sample_count = sorted_samples.size(),
        .minimum = sorted_samples.front(),
        .p50 = nearest_rank_percentile(sorted_samples, 50U),
        .p95 = nearest_rank_percentile(sorted_samples, 95U),
        .p99 = nearest_rank_percentile(sorted_samples, 99U),
        .maximum = sorted_samples.back(),
    };
}

std::expected<double, MetricsError>
calculate_throughput(std::size_t completed_operations, Latency elapsed)
{
    if (elapsed <= Latency::zero())
    {
        return std::unexpected(MetricsError::non_positive_elapsed_time);
    }

    const std::chrono::duration<double> elapsed_seconds = elapsed;
    return static_cast<double>(completed_operations) / elapsed_seconds.count();
}
} // namespace mcrs::benchmark

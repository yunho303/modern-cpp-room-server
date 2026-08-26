#include "mcrs/benchmark/metrics.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <source_location>
#include <string_view>

namespace
{
using namespace mcrs::benchmark;
using namespace std::chrono_literals;

int failure_count = 0;

void check(bool condition, std::string_view expression,
           const std::source_location location = std::source_location::current())
{
    if (condition)
    {
        return;
    }

    ++failure_count;
    std::cerr << location.file_name() << '(' << location.line()
              << "): CHECK failed: " << expression << '\n';
}

#define MCRS_CHECK(expression) check(static_cast<bool>(expression), #expression)

template <typename Function>
void run_test(std::string_view name, Function&& function)
{
    const auto failures_before = failure_count;
    function();
    std::cout << (failure_count == failures_before ? "[PASS] " : "[FAIL] ") << name << '\n';
}

void nearest_rank_percentiles_are_deterministic()
{
    std::array<Latency, 100> samples;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        samples[index] = std::chrono::duration_cast<Latency>(
            std::chrono::microseconds{static_cast<std::chrono::microseconds::rep>(100U - index)});
    }

    const auto summary = summarize_latencies(samples);

    MCRS_CHECK(summary.has_value());
    MCRS_CHECK(summary && summary->sample_count == 100U);
    MCRS_CHECK(summary && summary->minimum == 1us);
    MCRS_CHECK(summary && summary->p50 == 50us);
    MCRS_CHECK(summary && summary->p95 == 95us);
    MCRS_CHECK(summary && summary->p99 == 99us);
    MCRS_CHECK(summary && summary->maximum == 100us);
    MCRS_CHECK(samples.front() == 100us);
}

void invalid_latency_samples_are_rejected()
{
    const std::array<Latency, 0> empty_samples{};
    const std::array negative_samples{Latency{1}, Latency{-1}};

    const auto empty_result = summarize_latencies(empty_samples);
    const auto negative_result = summarize_latencies(negative_samples);

    MCRS_CHECK(!empty_result.has_value());
    MCRS_CHECK(empty_result.error() == MetricsError::no_samples);
    MCRS_CHECK(!negative_result.has_value());
    MCRS_CHECK(negative_result.error() == MetricsError::negative_latency);
}

void throughput_uses_elapsed_seconds()
{
    const auto throughput = calculate_throughput(250U, 2s);
    const auto zero_work = calculate_throughput(0U, 1s);
    const auto invalid_elapsed = calculate_throughput(1U, Latency::zero());

    MCRS_CHECK(throughput && std::abs(*throughput - 125.0) < 0.000'001);
    MCRS_CHECK(zero_work && *zero_work == 0.0);
    MCRS_CHECK(!invalid_elapsed.has_value());
    MCRS_CHECK(invalid_elapsed.error() == MetricsError::non_positive_elapsed_time);
}
} // namespace

int main()
{
    run_test("nearest-rank percentiles", nearest_rank_percentiles_are_deterministic);
    run_test("invalid latency samples", invalid_latency_samples_are_rejected);
    run_test("throughput per second", throughput_uses_elapsed_seconds);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "All benchmark metrics tests passed\n";
    return 0;
}

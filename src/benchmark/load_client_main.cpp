#include "mcrs/benchmark/load_client.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
template <typename T>
bool parse_unsigned(std::string_view text, T& output)
{
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        value > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
    {
        return false;
    }

    output = static_cast<T>(value);
    return true;
}

double microseconds(mcrs::benchmark::Latency latency)
{
    return std::chrono::duration<double, std::micro>{latency}.count();
}

void print_usage()
{
    std::cerr << "usage: mcrs_load_client [host] [port] [clients] [warmup] [measured]\n";
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc > 6)
    {
        print_usage();
        return 1;
    }

    mcrs::benchmark::LoadTestConfig config;
    if (argc >= 2)
    {
        config.host = argv[1];
    }
    if ((argc >= 3 && !parse_unsigned(std::string_view{argv[2]}, config.port)) ||
        (argc >= 4 && !parse_unsigned(std::string_view{argv[3]}, config.client_count)) ||
        (argc >= 5 &&
         !parse_unsigned(std::string_view{argv[4]}, config.warmup_moves_per_client)) ||
        (argc >= 6 &&
         !parse_unsigned(std::string_view{argv[5]}, config.measured_moves_per_client)))
    {
        print_usage();
        return 1;
    }

    const auto result = mcrs::benchmark::run_load_test(config);
    if (!result)
    {
        std::cerr << "load test failed: " << result.error() << '\n';
        return 1;
    }

    const auto elapsed_ms = std::chrono::duration<double, std::milli>{result->elapsed}.count();
    std::cout << std::fixed << std::setprecision(3)
              << "clients: " << result->config.client_count << '\n'
              << "warmup moves/client: " << result->config.warmup_moves_per_client << '\n'
              << "measured moves/client: " << result->config.measured_moves_per_client << '\n'
              << "samples: " << result->latency.sample_count << '\n'
              << "elapsed: " << elapsed_ms << " ms\n"
              << "commands/sec: " << result->commands_per_second << '\n'
              << "estimated deliveries/sec: " << result->estimated_deliveries_per_second << '\n'
              << "latency min/p50/p95/p99/max: "
              << microseconds(result->latency.minimum) << " / "
              << microseconds(result->latency.p50) << " / "
              << microseconds(result->latency.p95) << " / "
              << microseconds(result->latency.p99) << " / "
              << microseconds(result->latency.maximum) << " us\n";
    return 0;
}

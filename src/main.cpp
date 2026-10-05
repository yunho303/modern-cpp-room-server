#include "mcrs/network/server.hpp"
#include "mcrs/network/session_registry.hpp"
#include "mcrs/room/room_worker.hpp"

#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/system_error.hpp>
#include <asio/use_awaitable.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>

namespace
{
constexpr std::uint16_t default_port = 7777;

bool parse_port(std::string_view text, std::uint16_t &output) noexcept
{
    unsigned int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > 65'535U)
    {
        return false;
    }

    output = static_cast<std::uint16_t>(value);
    return true;
}

bool parse_interval(std::string_view text, std::uint32_t &output) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc{} && end == text.data() + text.size() && output <= 60'000U;
}

asio::awaitable<void> report_metrics(std::shared_ptr<asio::steady_timer> timer,
                                     std::shared_ptr<mcrs::observability::ServerMetrics> metrics,
                                     std::chrono::milliseconds interval)
{
    for (;;)
    {
        timer->expires_after(interval);
        asio::error_code error;
        co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, error));
        if (error == asio::error::operation_aborted)
        {
            co_return;
        }
        if (error)
        {
            throw asio::system_error{error};
        }
        mcrs::observability::write_json(std::cout, metrics->snapshot());
        std::cout << '\n' << std::flush;
    }
}
} // namespace

int main(int argc, char *argv[])
{
    std::uint16_t port = default_port;
    std::uint32_t metrics_interval_ms = 0;
    if (argc > 3 || (argc >= 2 && !parse_port(argv[1], port)) ||
        (argc == 3 && !parse_interval(argv[2], metrics_interval_ms)))
    {
        std::cerr << "usage: mcrs_server [port] [metrics_interval_ms: 0..60000; 0 disables]\n";
        return 1;
    }

    asio::io_context context{1};
    auto metrics = metrics_interval_ms == 0 ? std::shared_ptr<mcrs::observability::ServerMetrics>{}
                                            : std::make_shared<mcrs::observability::ServerMetrics>();
    mcrs::network::SessionRegistry session_registry{metrics};
    mcrs::room::RoomWorker room_worker{
        [&session_registry](const mcrs::room::RoomEvent &event) { session_registry.publish(event); }, metrics};
    bool server_failed = false;

    auto metrics_timer = metrics ? std::make_shared<asio::steady_timer>(context) : nullptr;
    if (metrics)
    {
        asio::co_spawn(context, report_metrics(metrics_timer, metrics, std::chrono::milliseconds{metrics_interval_ms}),
                       [](std::exception_ptr exception) {
                           if (exception)
                           {
                               std::cerr << "metrics reporter stopped by exception\n";
                           }
                       });
    }

    asio::co_spawn(context, mcrs::network::run_server(port, room_worker, session_registry),
                   [&server_failed, metrics_timer](std::exception_ptr exception) {
                       if (metrics_timer)
                       {
                           static_cast<void>(metrics_timer->cancel());
                       }
                       if (!exception)
                       {
                           return;
                       }

                       server_failed = true;
                       try
                       {
                           std::rethrow_exception(exception);
                       }
                       catch (const std::exception &error)
                       {
                           std::cerr << "server stopped by exception: " << error.what() << '\n';
                       }
                       catch (...)
                       {
                           std::cerr << "server stopped by an unknown exception\n";
                       }
                   });

    context.run();
    static_cast<void>(room_worker.stop());
    return server_failed ? 1 : 0;
}

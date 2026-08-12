#include "mcrs/network/server.hpp"

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string_view>

namespace
{
constexpr std::uint16_t default_port = 7777;

bool parse_port(std::string_view text, std::uint16_t& output) noexcept
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
} // namespace

int main(int argc, char* argv[])
{
    std::uint16_t port = default_port;
    if (argc > 2 || (argc == 2 && !parse_port(argv[1], port)))
    {
        std::cerr << "usage: mcrs_server [port]\n";
        return 1;
    }

    asio::io_context context{1};
    bool server_failed = false;

    asio::co_spawn(context, mcrs::network::run_server(port), [&server_failed](std::exception_ptr exception) {
        if (!exception)
        {
            return;
        }

        server_failed = true;
        try
        {
            std::rethrow_exception(exception);
        }
        catch (const std::exception& error)
        {
            std::cerr << "server stopped by exception: " << error.what() << '\n';
        }
        catch (...)
        {
            std::cerr << "server stopped by an unknown exception\n";
        }
    });

    context.run();
    return server_failed ? 1 : 0;
}

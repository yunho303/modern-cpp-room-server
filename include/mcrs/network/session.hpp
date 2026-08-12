#pragma once

#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>

namespace mcrs::network
{
[[nodiscard]] asio::awaitable<void> run_session(asio::ip::tcp::socket socket);
} // namespace mcrs::network

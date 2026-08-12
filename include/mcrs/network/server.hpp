#pragma once

#include <asio/awaitable.hpp>

#include <cstdint>

namespace mcrs::network
{
[[nodiscard]] asio::awaitable<void> run_server(std::uint16_t port);
} // namespace mcrs::network

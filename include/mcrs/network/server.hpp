#pragma once

#include <asio/awaitable.hpp>

#include "mcrs/room/room_worker.hpp"
#include "mcrs/network/session_registry.hpp"

#include <cstdint>

namespace mcrs::network
{
[[nodiscard]] asio::awaitable<void> run_server(std::uint16_t port, room::RoomWorker& room_worker,
                                               SessionRegistry& session_registry);
} // namespace mcrs::network

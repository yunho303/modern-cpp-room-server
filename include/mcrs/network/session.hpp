#pragma once

#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>

#include "mcrs/room/room_command.hpp"
#include "mcrs/room/room_worker.hpp"

namespace mcrs::network
{
[[nodiscard]] asio::awaitable<void> run_session(asio::ip::tcp::socket socket,
                                                room::SessionId session_id,
                                                room::RoomWorker& room_worker);
} // namespace mcrs::network

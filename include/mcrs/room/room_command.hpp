#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>

namespace mcrs::room
{
struct SessionId
{
    std::uint64_t value{};

    bool operator==(const SessionId&) const = default;
};

struct SessionIdHash
{
    [[nodiscard]] std::size_t operator()(SessionId session_id) const noexcept
    {
        return static_cast<std::size_t>(session_id.value);
    }
};

struct GridPosition
{
    std::int32_t x{};
    std::int32_t y{};

    bool operator==(const GridPosition&) const = default;
};

enum class DisconnectReason : std::uint16_t
{
    left_room = 1,
    client_closed = 2,
    protocol_error = 3,
    io_error = 4,
    outbound_overflow = 5,
    server_shutdown = 6,
};

struct JoinCommand
{
    SessionId session_id;
};

struct MoveCommand
{
    SessionId session_id;
    GridPosition destination;
};

struct LeaveCommand
{
    SessionId session_id;
    DisconnectReason reason;
};

using RoomCommand = std::variant<JoinCommand, MoveCommand, LeaveCommand>;
} // namespace mcrs::room

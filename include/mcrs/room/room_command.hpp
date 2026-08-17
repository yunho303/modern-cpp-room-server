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

enum class DisconnectReason
{
    left_room,
    client_closed,
    protocol_error,
    io_error,
    server_shutdown,
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

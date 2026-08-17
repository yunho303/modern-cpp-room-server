#pragma once

#include "mcrs/room/room_command.hpp"

#include <variant>

namespace mcrs::room
{
struct PlayerJoinedEvent
{
    SessionId session_id;
    GridPosition position;

    bool operator==(const PlayerJoinedEvent&) const = default;
};

struct PlayerMovedEvent
{
    SessionId session_id;
    GridPosition position;

    bool operator==(const PlayerMovedEvent&) const = default;
};

struct PlayerLeftEvent
{
    SessionId session_id;
    DisconnectReason reason;

    bool operator==(const PlayerLeftEvent&) const = default;
};

using RoomEvent = std::variant<PlayerJoinedEvent, PlayerMovedEvent, PlayerLeftEvent>;
} // namespace mcrs::room

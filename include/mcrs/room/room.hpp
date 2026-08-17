#pragma once

#include "mcrs/room/room_command.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mcrs::room
{
enum class RoomCommandError
{
    duplicate_session,
    session_not_found,
};

struct PlayerSnapshot
{
    SessionId session_id;
    GridPosition position;

    bool operator==(const PlayerSnapshot&) const = default;
};

// Room is intentionally not thread-safe. Its future worker thread will be its only owner.
class Room final
{
public:
    [[nodiscard]] std::expected<void, RoomCommandError> apply(const RoomCommand& command);

    [[nodiscard]] std::size_t player_count() const noexcept;
    [[nodiscard]] std::optional<PlayerSnapshot> find(SessionId session_id) const;
    [[nodiscard]] std::vector<PlayerSnapshot> snapshot() const;

private:
    [[nodiscard]] std::expected<void, RoomCommandError> apply_join(const JoinCommand& command);
    [[nodiscard]] std::expected<void, RoomCommandError> apply_move(const MoveCommand& command);
    [[nodiscard]] std::expected<void, RoomCommandError> apply_leave(const LeaveCommand& command);

    std::unordered_map<SessionId, GridPosition, SessionIdHash> players_;
};
} // namespace mcrs::room

#include "mcrs/room/room.hpp"

#include <algorithm>
#include <concepts>
#include <type_traits>

namespace mcrs::room
{
std::expected<RoomEvent, RoomCommandError> Room::apply(const RoomCommand& command)
{
    return std::visit(
        [this](const auto& concrete_command) -> std::expected<RoomEvent, RoomCommandError>
        {
            using Command = std::remove_cvref_t<decltype(concrete_command)>;

            if constexpr (std::same_as<Command, JoinCommand>)
            {
                return apply_join(concrete_command);
            }
            else if constexpr (std::same_as<Command, MoveCommand>)
            {
                return apply_move(concrete_command);
            }
            else
            {
                static_assert(std::same_as<Command, LeaveCommand>);
                return apply_leave(concrete_command);
            }
        },
        command);
}

std::size_t Room::player_count() const noexcept
{
    return players_.size();
}

std::optional<PlayerSnapshot> Room::find(SessionId session_id) const
{
    const auto player = players_.find(session_id);
    if (player == players_.end())
    {
        return std::nullopt;
    }

    return PlayerSnapshot{.session_id = player->first, .position = player->second};
}

std::vector<PlayerSnapshot> Room::snapshot() const
{
    std::vector<PlayerSnapshot> players;
    players.reserve(players_.size());

    for (const auto& [session_id, position] : players_)
    {
        players.push_back(PlayerSnapshot{.session_id = session_id, .position = position});
    }

    std::ranges::sort(players, {}, [](const PlayerSnapshot& player)
                      { return player.session_id.value; });
    return players;
}

std::expected<RoomEvent, RoomCommandError> Room::apply_join(const JoinCommand& command)
{
    const auto [player, inserted] = players_.try_emplace(command.session_id, GridPosition{});
    static_cast<void>(player);

    if (!inserted)
    {
        return std::unexpected(RoomCommandError::duplicate_session);
    }

    return PlayerJoinedEvent{
        .session_id = command.session_id,
        .position = GridPosition{},
    };
}

std::expected<RoomEvent, RoomCommandError> Room::apply_move(const MoveCommand& command)
{
    const auto player = players_.find(command.session_id);
    if (player == players_.end())
    {
        return std::unexpected(RoomCommandError::session_not_found);
    }

    player->second = command.destination;
    return PlayerMovedEvent{
        .session_id = command.session_id,
        .position = command.destination,
    };
}

std::expected<RoomEvent, RoomCommandError> Room::apply_leave(const LeaveCommand& command)
{
    if (players_.erase(command.session_id) == 0)
    {
        return std::unexpected(RoomCommandError::session_not_found);
    }

    return PlayerLeftEvent{
        .session_id = command.session_id,
        .reason = command.reason,
    };
}
} // namespace mcrs::room

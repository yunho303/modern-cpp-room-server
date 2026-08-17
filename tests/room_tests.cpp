#include "mcrs/room/room.hpp"

#include <iostream>
#include <source_location>
#include <string_view>

namespace
{
using namespace mcrs::room;

int failure_count = 0;

void check(bool condition, std::string_view expression,
           const std::source_location location = std::source_location::current())
{
    if (condition)
    {
        return;
    }

    ++failure_count;
    std::cerr << location.file_name() << '(' << location.line() << "): CHECK failed: " << expression << '\n';
}

#define MCRS_CHECK(expression) check(static_cast<bool>(expression), #expression)

template <typename Function>
void run_test(std::string_view name, Function&& function)
{
    const auto failures_before = failure_count;
    function();
    std::cout << (failure_count == failures_before ? "[PASS] " : "[FAIL] ") << name << '\n';
}

void join_adds_a_player_at_the_default_position()
{
    Room room;
    constexpr SessionId session_id{101};

    const auto result = room.apply(JoinCommand{.session_id = session_id});
    const auto player = room.find(session_id);

    MCRS_CHECK(result.has_value());
    MCRS_CHECK(room.player_count() == 1);
    MCRS_CHECK(player.has_value());
    MCRS_CHECK(player && player->position == GridPosition{});
}

void duplicate_join_is_rejected_without_replacing_state()
{
    Room room;
    constexpr SessionId session_id{102};
    constexpr GridPosition destination{4, 7};

    MCRS_CHECK(room.apply(JoinCommand{.session_id = session_id}).has_value());
    MCRS_CHECK(room.apply(MoveCommand{.session_id = session_id, .destination = destination}).has_value());

    const auto duplicate = room.apply(JoinCommand{.session_id = session_id});
    const auto player = room.find(session_id);

    MCRS_CHECK(!duplicate.has_value());
    MCRS_CHECK(duplicate.error() == RoomCommandError::duplicate_session);
    MCRS_CHECK(room.player_count() == 1);
    MCRS_CHECK(player && player->position == destination);
}

void move_updates_only_an_existing_player()
{
    Room room;
    constexpr SessionId joined_session{103};
    constexpr SessionId unknown_session{104};
    constexpr GridPosition destination{-3, 12};

    MCRS_CHECK(room.apply(JoinCommand{.session_id = joined_session}).has_value());
    const auto moved = room.apply(MoveCommand{.session_id = joined_session, .destination = destination});
    const auto rejected = room.apply(MoveCommand{.session_id = unknown_session, .destination = destination});

    MCRS_CHECK(moved.has_value());
    MCRS_CHECK(room.find(joined_session)->position == destination);
    MCRS_CHECK(!rejected.has_value());
    MCRS_CHECK(rejected.error() == RoomCommandError::session_not_found);
}

void leave_removes_the_player()
{
    Room room;
    constexpr SessionId session_id{105};

    MCRS_CHECK(room.apply(JoinCommand{.session_id = session_id}).has_value());
    const auto left = room.apply(LeaveCommand{
        .session_id = session_id,
        .reason = DisconnectReason::client_closed,
    });

    MCRS_CHECK(left.has_value());
    MCRS_CHECK(room.player_count() == 0);
    MCRS_CHECK(!room.find(session_id).has_value());

    const auto duplicate_leave = room.apply(LeaveCommand{
        .session_id = session_id,
        .reason = DisconnectReason::client_closed,
    });
    MCRS_CHECK(!duplicate_leave.has_value());
    MCRS_CHECK(duplicate_leave.error() == RoomCommandError::session_not_found);
}
} // namespace

int main()
{
    run_test("join adds a player", join_adds_a_player_at_the_default_position);
    run_test("duplicate join is rejected", duplicate_join_is_rejected_without_replacing_state);
    run_test("move requires an existing player", move_updates_only_an_existing_player);
    run_test("leave removes a player", leave_removes_the_player);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "All Room tests passed\n";
    return 0;
}

#include "mcrs/network/room_event_encoder.hpp"
#include "mcrs/network/session_registry.hpp"
#include "mcrs/protocol/packet_codec.hpp"

#include <iostream>
#include <memory>
#include <source_location>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace mcrs::network;
using namespace mcrs::protocol;
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

class RecordingEndpoint final : public SessionOutboundEndpoint
{
public:
    void deliver(SharedPacket packet) override
    {
        packets.push_back(std::move(packet));
    }

    std::vector<SharedPacket> packets;
};

PacketType packet_type(const SharedPacket& packet)
{
    const auto decoded = decode_one(*packet);
    return decoded ? decoded->header.type : static_cast<PacketType>(0);
}

void event_encoder_preserves_event_type()
{
    const auto joined = encode_room_event(PlayerJoinedEvent{
        .session_id = SessionId{7},
        .position = GridPosition{3, 4},
    });
    const auto moved = encode_room_event(PlayerMovedEvent{
        .session_id = SessionId{7},
        .position = GridPosition{-5, 6},
    });
    const auto left = encode_room_event(PlayerLeftEvent{
        .session_id = SessionId{7},
        .reason = DisconnectReason::client_closed,
    });

    MCRS_CHECK(joined && packet_type(*joined) == PacketType::player_joined);
    MCRS_CHECK(moved && packet_type(*moved) == PacketType::player_moved);
    MCRS_CHECK(left && packet_type(*left) == PacketType::player_left);
}

void registry_broadcasts_only_to_current_room_members()
{
    SessionRegistry registry;
    auto first = std::make_shared<RecordingEndpoint>();
    auto second = std::make_shared<RecordingEndpoint>();
    constexpr SessionId first_id{11};
    constexpr SessionId second_id{12};

    MCRS_CHECK(registry.register_session(first_id, first));
    MCRS_CHECK(registry.register_session(second_id, second));

    registry.publish(PlayerJoinedEvent{.session_id = first_id, .position = {}});
    registry.publish(PlayerJoinedEvent{.session_id = second_id, .position = {}});
    registry.publish(PlayerMovedEvent{.session_id = first_id, .position = {9, 10}});
    registry.publish(PlayerLeftEvent{
        .session_id = first_id,
        .reason = DisconnectReason::left_room,
    });

    MCRS_CHECK(registry.connected_count() == 2);
    MCRS_CHECK(registry.room_member_count() == 1);
    MCRS_CHECK(first->packets.size() == 3);
    MCRS_CHECK(second->packets.size() == 3);
    MCRS_CHECK(packet_type(first->packets[0]) == PacketType::player_joined);
    MCRS_CHECK(packet_type(first->packets[2]) == PacketType::player_moved);
    MCRS_CHECK(packet_type(second->packets[2]) == PacketType::player_left);

    MCRS_CHECK(first->packets[1].get() == second->packets[0].get());
    MCRS_CHECK(first->packets[2].get() == second->packets[1].get());
}
} // namespace

int main()
{
    run_test("Room event encoding", event_encoder_preserves_event_type);
    run_test("Registry room membership broadcast", registry_broadcasts_only_to_current_room_members);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "All Session Registry tests passed\n";
    return 0;
}

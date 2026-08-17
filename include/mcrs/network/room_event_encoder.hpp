#pragma once

#include "mcrs/network/outbound_queue.hpp"
#include "mcrs/protocol/packet_codec.hpp"
#include "mcrs/room/room_event.hpp"

#include <expected>

namespace mcrs::network
{
[[nodiscard]] std::expected<SharedPacket, protocol::EncodeError>
encode_room_event(const room::RoomEvent& event);
} // namespace mcrs::network

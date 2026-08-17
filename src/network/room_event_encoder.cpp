#include "mcrs/network/room_event_encoder.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace mcrs::network
{
namespace
{
inline constexpr std::size_t player_state_payload_size =
    sizeof(std::uint64_t) + sizeof(std::int32_t) * 2U;
inline constexpr std::size_t player_left_payload_size =
    sizeof(std::uint64_t) + sizeof(std::uint16_t);

template <typename T>
concept WireUnsignedInteger = std::unsigned_integral<T> &&
                              (sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

template <WireUnsignedInteger T>
void write_big_endian(std::span<std::byte> bytes, std::size_t offset, T value) noexcept
{
    for (std::size_t index = 0; index < sizeof(T); ++index)
    {
        const auto shift = static_cast<unsigned int>((sizeof(T) - index - 1U) * 8U);
        bytes[offset + index] = static_cast<std::byte>((value >> shift) & static_cast<T>(0xFFU));
    }
}

void write_session_id(std::span<std::byte> payload, room::SessionId session_id) noexcept
{
    write_big_endian(payload, 0, session_id.value);
}

void write_position(std::span<std::byte> payload, room::GridPosition position) noexcept
{
    write_big_endian(payload, sizeof(std::uint64_t), std::bit_cast<std::uint32_t>(position.x));
    write_big_endian(payload, sizeof(std::uint64_t) + sizeof(std::int32_t),
                     std::bit_cast<std::uint32_t>(position.y));
}

[[nodiscard]] std::expected<SharedPacket, protocol::EncodeError>
encode_player_state(protocol::PacketType type, room::SessionId session_id,
                    room::GridPosition position)
{
    std::vector<std::byte> payload(player_state_payload_size);
    write_session_id(payload, session_id);
    write_position(payload, position);

    auto encoded = protocol::encode_packet(type, payload);
    if (!encoded)
    {
        return std::unexpected(encoded.error());
    }

    return make_shared_packet(std::move(*encoded));
}
} // namespace

std::expected<SharedPacket, protocol::EncodeError>
encode_room_event(const room::RoomEvent& event)
{
    return std::visit(
        [](const auto& concrete_event) -> std::expected<SharedPacket, protocol::EncodeError>
        {
            using Event = std::remove_cvref_t<decltype(concrete_event)>;

            if constexpr (std::same_as<Event, room::PlayerJoinedEvent>)
            {
                return encode_player_state(protocol::PacketType::player_joined,
                                           concrete_event.session_id, concrete_event.position);
            }
            else if constexpr (std::same_as<Event, room::PlayerMovedEvent>)
            {
                return encode_player_state(protocol::PacketType::player_moved,
                                           concrete_event.session_id, concrete_event.position);
            }
            else
            {
                static_assert(std::same_as<Event, room::PlayerLeftEvent>);

                std::vector<std::byte> payload(player_left_payload_size);
                write_session_id(payload, concrete_event.session_id);
                write_big_endian(std::span{payload}, sizeof(std::uint64_t),
                                 static_cast<std::uint16_t>(concrete_event.reason));

                auto encoded = protocol::encode_packet(protocol::PacketType::player_left, payload);
                if (!encoded)
                {
                    return std::unexpected(encoded.error());
                }

                return make_shared_packet(std::move(*encoded));
            }
        },
        event);
}
} // namespace mcrs::network

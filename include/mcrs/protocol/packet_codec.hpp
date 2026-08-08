#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace mcrs::protocol
{
enum class PacketType : std::uint16_t
{
    ping = 1,
    join_room = 2,
    move = 3,
    attack = 4,
    leave_room = 5,
};

inline constexpr std::size_t wire_header_size = 6;
inline constexpr std::uint32_t max_payload_size = 64U * 1024U;

struct PacketHeader
{
    std::uint32_t payload_size{};
    PacketType type{};

    bool operator==(const PacketHeader&) const = default;
};

enum class DecodeError
{
    incomplete_header,
    payload_too_large,
    unknown_packet_type,
    incomplete_payload,
};

enum class EncodeError
{
    payload_too_large,
    unknown_packet_type,
};

// The payload is a non-owning view into the input buffer passed to decode_one.
struct PacketView
{
    PacketHeader header;
    std::span<const std::byte> payload;
    std::size_t consumed_bytes{};
};

[[nodiscard]] std::expected<PacketView, DecodeError> decode_one(std::span<const std::byte> bytes) noexcept;

[[nodiscard]] std::expected<std::vector<std::byte>, EncodeError>
encode_packet(PacketType type, std::span<const std::byte> payload);

[[nodiscard]] bool is_incomplete(DecodeError error) noexcept;
[[nodiscard]] std::string_view to_string(DecodeError error) noexcept;
[[nodiscard]] std::string_view to_string(EncodeError error) noexcept;
} // namespace mcrs::protocol

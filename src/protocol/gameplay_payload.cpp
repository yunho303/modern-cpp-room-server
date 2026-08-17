#include "mcrs/protocol/gameplay_payload.hpp"

#include <bit>
#include <cstdint>

namespace mcrs::protocol
{
namespace
{
[[nodiscard]] std::uint32_t read_uint32_big_endian(std::span<const std::byte> bytes,
                                                    std::size_t offset) noexcept
{
    return (std::to_integer<std::uint32_t>(bytes[offset]) << 24U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           std::to_integer<std::uint32_t>(bytes[offset + 3U]);
}

void write_uint32_big_endian(std::span<std::byte> bytes, std::size_t offset,
                             std::uint32_t value) noexcept
{
    bytes[offset] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::byte>(value & 0xFFU);
}
} // namespace

std::expected<void, GameplayPayloadError>
validate_empty_payload(std::span<const std::byte> payload) noexcept
{
    if (!payload.empty())
    {
        return std::unexpected(GameplayPayloadError::expected_empty_payload);
    }

    return {};
}

std::expected<MovePayload, GameplayPayloadError>
decode_move_payload(std::span<const std::byte> payload) noexcept
{
    if (payload.size() != move_payload_size)
    {
        return std::unexpected(GameplayPayloadError::invalid_move_payload_size);
    }

    const auto x_bits = read_uint32_big_endian(payload, 0);
    const auto y_bits = read_uint32_big_endian(payload, sizeof(std::int32_t));
    return MovePayload{
        .x = std::bit_cast<std::int32_t>(x_bits),
        .y = std::bit_cast<std::int32_t>(y_bits),
    };
}

std::array<std::byte, move_payload_size> encode_move_payload(MovePayload payload) noexcept
{
    std::array<std::byte, move_payload_size> encoded{};
    write_uint32_big_endian(encoded, 0, std::bit_cast<std::uint32_t>(payload.x));
    write_uint32_big_endian(encoded, sizeof(std::int32_t), std::bit_cast<std::uint32_t>(payload.y));
    return encoded;
}

std::string_view to_string(GameplayPayloadError error) noexcept
{
    switch (error)
    {
    case GameplayPayloadError::expected_empty_payload:
        return "packet payload must be empty";
    case GameplayPayloadError::invalid_move_payload_size:
        return "move payload must contain two 32-bit coordinates";
    }

    return "unknown gameplay payload error";
}
} // namespace mcrs::protocol

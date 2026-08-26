#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace mcrs::protocol
{
inline constexpr std::size_t move_payload_size = sizeof(std::int32_t) * 2U;
inline constexpr std::size_t player_state_payload_size =
    sizeof(std::uint64_t) + sizeof(std::int32_t) * 2U;
inline constexpr std::size_t player_left_payload_size =
    sizeof(std::uint64_t) + sizeof(std::uint16_t);

struct MovePayload
{
    std::int32_t x{};
    std::int32_t y{};

    bool operator==(const MovePayload&) const = default;
};

struct PlayerStatePayload
{
    std::uint64_t session_id{};
    std::int32_t x{};
    std::int32_t y{};

    bool operator==(const PlayerStatePayload&) const = default;
};

struct PlayerLeftPayload
{
    std::uint64_t session_id{};
    std::uint16_t reason{};

    bool operator==(const PlayerLeftPayload&) const = default;
};

enum class GameplayPayloadError
{
    expected_empty_payload,
    invalid_move_payload_size,
    invalid_player_state_payload_size,
    invalid_player_left_payload_size,
};

[[nodiscard]] std::expected<void, GameplayPayloadError>
validate_empty_payload(std::span<const std::byte> payload) noexcept;

[[nodiscard]] std::expected<MovePayload, GameplayPayloadError>
decode_move_payload(std::span<const std::byte> payload) noexcept;

[[nodiscard]] std::array<std::byte, move_payload_size> encode_move_payload(MovePayload payload) noexcept;

[[nodiscard]] std::expected<PlayerStatePayload, GameplayPayloadError>
decode_player_state_payload(std::span<const std::byte> payload) noexcept;

[[nodiscard]] std::expected<PlayerLeftPayload, GameplayPayloadError>
decode_player_left_payload(std::span<const std::byte> payload) noexcept;

[[nodiscard]] std::string_view to_string(GameplayPayloadError error) noexcept;
} // namespace mcrs::protocol

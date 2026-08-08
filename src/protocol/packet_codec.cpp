#include "mcrs/protocol/packet_codec.hpp"

#include <bit>
#include <concepts>
#include <cstring>
#include <limits>
#include <type_traits>

namespace mcrs::protocol
{
namespace
{
template <typename T>
concept WireInteger = std::unsigned_integral<T> && (sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

template <WireInteger T>
[[nodiscard]] T read_big_endian(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));

    if constexpr (std::endian::native == std::endian::little)
    {
        return std::byteswap(value);
    }
    else
    {
        return value;
    }
}

template <WireInteger T>
void write_big_endian(std::span<std::byte> bytes, std::size_t offset, T value) noexcept
{
    if constexpr (std::endian::native == std::endian::little)
    {
        value = std::byteswap(value);
    }

    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

[[nodiscard]] bool is_known(PacketType type) noexcept
{
    switch (type)
    {
    case PacketType::ping:
    case PacketType::join_room:
    case PacketType::move:
    case PacketType::attack:
    case PacketType::leave_room:
        return true;
    }

    return false;
}
} // namespace

std::expected<PacketView, DecodeError> decode_one(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() < wire_header_size)
    {
        return std::unexpected(DecodeError::incomplete_header);
    }

    const auto payload_size = read_big_endian<std::uint32_t>(bytes, 0);
    const auto type = static_cast<PacketType>(read_big_endian<std::uint16_t>(bytes, 4));
    const auto sequence = read_big_endian<std::uint32_t>(bytes, 6);

    if (payload_size > max_payload_size)
    {
        return std::unexpected(DecodeError::payload_too_large);
    }

    if (!is_known(type))
    {
        return std::unexpected(DecodeError::unknown_packet_type);
    }

    const auto total_size = wire_header_size + static_cast<std::size_t>(payload_size);
    if (bytes.size() < total_size)
    {
        return std::unexpected(DecodeError::incomplete_payload);
    }

    return PacketView{
        .header = PacketHeader{.payload_size = payload_size, .type = type, .sequence = sequence},
        .payload = bytes.subspan(wire_header_size, payload_size),
        .consumed_bytes = total_size,
    };
}

std::expected<std::vector<std::byte>, EncodeError>
encode_packet(PacketType type, std::uint32_t sequence, std::span<const std::byte> payload)
{
    if (payload.size() > max_payload_size || payload.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected(EncodeError::payload_too_large);
    }

    if (!is_known(type))
    {
        return std::unexpected(EncodeError::unknown_packet_type);
    }

    std::vector<std::byte> encoded(wire_header_size + payload.size());
    const auto output = std::span{encoded};

    write_big_endian<std::uint32_t>(output, 0, static_cast<std::uint32_t>(payload.size()));
    write_big_endian<std::uint16_t>(output, 4, static_cast<std::uint16_t>(type));
    write_big_endian<std::uint32_t>(output, 6, sequence);

    if (!payload.empty())
    {
        std::memcpy(encoded.data() + wire_header_size, payload.data(), payload.size());
    }

    return encoded;
}

bool is_incomplete(DecodeError error) noexcept
{
    return error == DecodeError::incomplete_header || error == DecodeError::incomplete_payload;
}

std::string_view to_string(DecodeError error) noexcept
{
    switch (error)
    {
    case DecodeError::incomplete_header:
        return "incomplete header";
    case DecodeError::payload_too_large:
        return "payload exceeds configured limit";
    case DecodeError::unknown_packet_type:
        return "unknown packet type";
    case DecodeError::incomplete_payload:
        return "incomplete payload";
    }

    return "unknown decode error";
}

std::string_view to_string(EncodeError error) noexcept
{
    switch (error)
    {
    case EncodeError::payload_too_large:
        return "payload exceeds configured limit";
    case EncodeError::unknown_packet_type:
        return "unknown packet type";
    }

    return "unknown encode error";
}
} // namespace mcrs::protocol

#include "mcrs/protocol/packet_codec.hpp"
#include "mcrs/protocol/gameplay_payload.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <source_location>
#include <span>
#include <string_view>
#include <vector>

namespace
{
using namespace mcrs::protocol;

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

void round_trip_preserves_header_and_payload()
{
    constexpr std::array payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    const auto encoded = encode_packet(PacketType::move, payload);

    MCRS_CHECK(encoded.has_value());
    if (!encoded)
    {
        return;
    }

    const auto decoded = decode_one(*encoded);
    MCRS_CHECK(decoded.has_value());
    if (!decoded)
    {
        return;
    }

    MCRS_CHECK((decoded->header == PacketHeader{3, PacketType::move}));
    MCRS_CHECK(std::ranges::equal(decoded->payload, payload));
    MCRS_CHECK(decoded->consumed_bytes == encoded->size());
}

void header_uses_big_endian_wire_order()
{
    constexpr std::array payload{std::byte{0xAA}, std::byte{0xBB}};
    const auto encoded = encode_packet(PacketType::attack, payload);

    MCRS_CHECK(encoded.has_value());
    if (!encoded)
    {
        return;
    }

    constexpr std::array expected_header{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x04},
    };

    MCRS_CHECK(std::ranges::equal(std::span{*encoded}.first(wire_header_size), expected_header));
}

void incomplete_header_requests_more_data()
{
    const std::array<std::byte, wire_header_size - 1> partial{};
    const auto decoded = decode_one(partial);

    MCRS_CHECK(!decoded.has_value());
    MCRS_CHECK(decoded.error() == DecodeError::incomplete_header);
    MCRS_CHECK(is_incomplete(decoded.error()));
}

void incomplete_payload_requests_more_data()
{
    constexpr std::array payload{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    const auto encoded = encode_packet(PacketType::ping, payload);
    MCRS_CHECK(encoded.has_value());
    if (!encoded)
    {
        return;
    }

    const auto partial = std::span{*encoded}.first(encoded->size() - 1);
    const auto decoded = decode_one(partial);

    MCRS_CHECK(!decoded.has_value());
    MCRS_CHECK(decoded.error() == DecodeError::incomplete_payload);
    MCRS_CHECK(is_incomplete(decoded.error()));
}

void oversized_payload_is_rejected_before_allocation()
{
    constexpr std::array<std::byte, wire_header_size> header{
        std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x01},
    };

    const auto decoded = decode_one(header);
    MCRS_CHECK(!decoded.has_value());
    MCRS_CHECK(decoded.error() == DecodeError::payload_too_large);
    MCRS_CHECK(!is_incomplete(decoded.error()));
}

void unknown_packet_type_is_rejected()
{
    constexpr std::array<std::byte, wire_header_size> header{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x7F}, std::byte{0xFF},
    };

    const auto decoded = decode_one(header);
    MCRS_CHECK(!decoded.has_value());
    MCRS_CHECK(decoded.error() == DecodeError::unknown_packet_type);
}

void concatenated_packets_are_consumed_one_at_a_time()
{
    constexpr std::array first_payload{std::byte{0x11}};
    constexpr std::array second_payload{std::byte{0x22}, std::byte{0x33}};
    const auto first = encode_packet(PacketType::ping, first_payload);
    const auto second = encode_packet(PacketType::move, second_payload);

    MCRS_CHECK(first.has_value());
    MCRS_CHECK(second.has_value());
    if (!first || !second)
    {
        return;
    }

    std::vector<std::byte> stream;
    stream.reserve(first->size() + second->size());
    stream.insert(stream.end(), first->begin(), first->end());
    stream.insert(stream.end(), second->begin(), second->end());

    const auto decoded_first = decode_one(stream);
    MCRS_CHECK(decoded_first.has_value());
    if (!decoded_first)
    {
        return;
    }

    const auto remaining = std::span{stream}.subspan(decoded_first->consumed_bytes);
    const auto decoded_second = decode_one(remaining);

    MCRS_CHECK(decoded_first->header.type == PacketType::ping);
    MCRS_CHECK(decoded_second.has_value());
    MCRS_CHECK(decoded_second && decoded_second->header.type == PacketType::move);
    MCRS_CHECK(decoded_second && std::ranges::equal(decoded_second->payload, second_payload));
}

void empty_payload_is_supported()
{
    const auto encoded = encode_packet(PacketType::leave_room, {});
    MCRS_CHECK(encoded.has_value());
    if (!encoded)
    {
        return;
    }

    const auto decoded = decode_one(*encoded);
    MCRS_CHECK(decoded.has_value());
    MCRS_CHECK(decoded && decoded->payload.empty());
    MCRS_CHECK(decoded && decoded->consumed_bytes == wire_header_size);
}

void move_payload_preserves_signed_coordinates()
{
    constexpr MovePayload movement{.x = -120, .y = 45'678};
    const auto encoded = encode_move_payload(movement);
    const auto decoded = decode_move_payload(encoded);

    MCRS_CHECK(decoded.has_value());
    MCRS_CHECK(decoded && *decoded == movement);
}

void move_payload_requires_exact_coordinate_size()
{
    constexpr std::array<std::byte, move_payload_size - 1U> undersized{};
    const auto decoded = decode_move_payload(undersized);

    MCRS_CHECK(!decoded.has_value());
    MCRS_CHECK(decoded.error() == GameplayPayloadError::invalid_move_payload_size);
}
} // namespace

int main()
{
    run_test("round trip", round_trip_preserves_header_and_payload);
    run_test("wire byte order", header_uses_big_endian_wire_order);
    run_test("incomplete header", incomplete_header_requests_more_data);
    run_test("incomplete payload", incomplete_payload_requests_more_data);
    run_test("oversized payload", oversized_payload_is_rejected_before_allocation);
    run_test("unknown packet type", unknown_packet_type_is_rejected);
    run_test("concatenated packets", concatenated_packets_are_consumed_one_at_a_time);
    run_test("empty payload", empty_payload_is_supported);
    run_test("signed move payload", move_payload_preserves_signed_coordinates);
    run_test("invalid move payload size", move_payload_requires_exact_coordinate_size);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "All protocol codec tests passed\n";
    return 0;
}

#include "mcrs/network/receive_buffer.hpp"
#include "mcrs/protocol/packet_codec.hpp"

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
using mcrs::network::ReceiveBuffer;
using mcrs::network::ReceiveBufferError;
using namespace mcrs::protocol;

int failure_count = 0;
constexpr std::size_t receive_buffer_limit = 128U * 1024U;

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

void split_packet_is_retained_until_complete()
{
    constexpr std::array payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    const auto encoded = encode_packet(PacketType::move, payload);
    MCRS_CHECK(encoded.has_value());
    if (!encoded)
    {
        return;
    }

    ReceiveBuffer buffer{receive_buffer_limit, 4};
    constexpr std::size_t first_read_size = 4;
    const auto first_append = buffer.append(std::span{*encoded}.first(first_read_size));
    MCRS_CHECK(first_append.has_value());

    const auto incomplete = decode_one(buffer.readable_bytes());
    MCRS_CHECK(!incomplete.has_value());
    MCRS_CHECK(incomplete.error() == DecodeError::incomplete_header);
    MCRS_CHECK(buffer.size() == first_read_size);

    const auto second_append = buffer.append(std::span{*encoded}.subspan(first_read_size));
    MCRS_CHECK(second_append.has_value());
    const auto complete = decode_one(buffer.readable_bytes());

    MCRS_CHECK(complete.has_value());
    MCRS_CHECK(complete && complete->header.type == PacketType::move);
    MCRS_CHECK(complete && std::ranges::equal(complete->payload, payload));

    if (complete)
    {
        buffer.consume(complete->consumed_bytes);
    }
    MCRS_CHECK(buffer.empty());
}

void concatenated_packets_are_consumed_in_order()
{
    constexpr std::array first_payload{std::byte{0x01}};
    constexpr std::array second_payload{std::byte{0x02}, std::byte{0x03}};
    const auto first = encode_packet(PacketType::ping, first_payload);
    const auto second = encode_packet(PacketType::attack, second_payload);
    MCRS_CHECK(first.has_value());
    MCRS_CHECK(second.has_value());
    if (!first || !second)
    {
        return;
    }

    std::vector<std::byte> received;
    received.reserve(first->size() + second->size());
    received.insert(received.end(), first->begin(), first->end());
    received.insert(received.end(), second->begin(), second->end());

    ReceiveBuffer buffer{receive_buffer_limit};
    const auto append_result = buffer.append(received);
    MCRS_CHECK(append_result.has_value());

    std::array<PacketType, 2> decoded_types{};
    std::size_t decoded_count = 0;
    while (!buffer.empty())
    {
        const auto decoded = decode_one(buffer.readable_bytes());
        MCRS_CHECK(decoded.has_value());
        if (!decoded)
        {
            break;
        }

        decoded_types.at(decoded_count++) = decoded->header.type;
        buffer.consume(decoded->consumed_bytes);
    }

    MCRS_CHECK(decoded_count == 2);
    MCRS_CHECK(decoded_types[0] == PacketType::ping);
    MCRS_CHECK(decoded_types[1] == PacketType::attack);
    MCRS_CHECK(buffer.empty());
}

void consumed_prefix_is_reused_for_later_reads()
{
    constexpr std::array first_payload{std::byte{0x11}, std::byte{0x12}};
    constexpr std::array second_payload{std::byte{0x21}, std::byte{0x22}, std::byte{0x23}};
    const auto first = encode_packet(PacketType::join_room, first_payload);
    const auto second = encode_packet(PacketType::leave_room, second_payload);
    MCRS_CHECK(first.has_value());
    MCRS_CHECK(second.has_value());
    if (!first || !second)
    {
        return;
    }

    constexpr std::size_t second_prefix_size = 3;
    std::vector<std::byte> first_read;
    first_read.reserve(first->size() + second_prefix_size);
    first_read.insert(first_read.end(), first->begin(), first->end());
    first_read.insert(first_read.end(), second->begin(), second->begin() + second_prefix_size);

    ReceiveBuffer buffer{receive_buffer_limit, first_read.size()};
    const auto first_append = buffer.append(first_read);
    MCRS_CHECK(first_append.has_value());

    const auto decoded_first = decode_one(buffer.readable_bytes());
    MCRS_CHECK(decoded_first.has_value());
    if (!decoded_first)
    {
        return;
    }

    buffer.consume(decoded_first->consumed_bytes);
    MCRS_CHECK(buffer.size() == second_prefix_size);

    const auto second_append = buffer.append(std::span{*second}.subspan(second_prefix_size));
    MCRS_CHECK(second_append.has_value());
    const auto decoded_second = decode_one(buffer.readable_bytes());

    MCRS_CHECK(decoded_second.has_value());
    MCRS_CHECK(decoded_second && decoded_second->header.type == PacketType::leave_room);
    MCRS_CHECK(decoded_second && std::ranges::equal(decoded_second->payload, second_payload));
}

void invalid_packet_is_left_for_the_session_to_reject()
{
    constexpr std::array<std::byte, wire_header_size> unknown_type_header{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x7F}, std::byte{0xFF},
    };

    ReceiveBuffer buffer{receive_buffer_limit};
    const auto append_result = buffer.append(unknown_type_header);
    MCRS_CHECK(append_result.has_value());
    const auto decoded = decode_one(buffer.readable_bytes());

    MCRS_CHECK(!decoded.has_value());
    MCRS_CHECK(decoded.error() == DecodeError::unknown_packet_type);
    MCRS_CHECK(buffer.size() == unknown_type_header.size());
}

void buffer_limit_is_checked_before_growth()
{
    constexpr std::array first_read{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
        std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
    };
    constexpr std::array second_read{
        std::byte{0x07}, std::byte{0x08}, std::byte{0x09},
    };

    ReceiveBuffer buffer{8, 4};
    const auto first_append = buffer.append(first_read);
    const auto rejected_append = buffer.append(second_read);

    MCRS_CHECK(first_append.has_value());
    MCRS_CHECK(!rejected_append.has_value());
    MCRS_CHECK(rejected_append.error() == ReceiveBufferError::buffer_limit_exceeded);
    MCRS_CHECK(buffer.size() == first_read.size());
    MCRS_CHECK(buffer.max_size() == 8);
    MCRS_CHECK(std::ranges::equal(buffer.readable_bytes(), first_read));
}
} // namespace

int main()
{
    run_test("split packet is retained", split_packet_is_retained_until_complete);
    run_test("concatenated packets are consumed", concatenated_packets_are_consumed_in_order);
    run_test("consumed prefix is reused", consumed_prefix_is_reused_for_later_reads);
    run_test("invalid packet remains for rejection", invalid_packet_is_left_for_the_session_to_reject);
    run_test("buffer limit is enforced", buffer_limit_is_checked_before_growth);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "All receive buffer tests passed\n";
    return 0;
}

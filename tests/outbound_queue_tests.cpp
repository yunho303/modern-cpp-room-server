#include "mcrs/network/outbound_queue.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <source_location>
#include <string_view>

namespace
{
using namespace mcrs::network;

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

SharedPacket packet_with_size(std::size_t size)
{
    return make_shared_packet(PacketBuffer(size, std::byte{0x5A}));
}

void one_immutable_packet_can_be_shared_by_multiple_queues()
{
    OutboundQueue first{64};
    OutboundQueue second{64};
    const auto packet = packet_with_size(8);

    MCRS_CHECK(first.push(packet).has_value());
    MCRS_CHECK(second.push(packet).has_value());

    std::array<SharedPacket, 1> first_batch;
    std::array<SharedPacket, 1> second_batch;
    const auto first_count = first.pop_batch(first_batch, 64);
    const auto second_count = second.pop_batch(second_batch, 64);

    MCRS_CHECK(first_count == 1);
    MCRS_CHECK(second_count == 1);
    MCRS_CHECK(first_batch[0].get() == packet.get());
    MCRS_CHECK(second_batch[0].get() == packet.get());
    MCRS_CHECK(first.pending_bytes() == 0);
    MCRS_CHECK(second.pending_bytes() == 0);
}

void pending_byte_limit_rejects_a_slow_consumer()
{
    OutboundQueue queue{10};
    const auto six_bytes = packet_with_size(6);
    const auto five_bytes = packet_with_size(5);

    MCRS_CHECK(queue.push(six_bytes).has_value());
    const auto rejected = queue.push(five_bytes);

    MCRS_CHECK(!rejected.has_value());
    MCRS_CHECK(rejected.error() == OutboundQueueError::byte_limit_exceeded);
    MCRS_CHECK(queue.pending_bytes() == 6);

    std::array<SharedPacket, 1> batch;
    MCRS_CHECK(queue.pop_batch(batch, 10) == 1);
    MCRS_CHECK(queue.push(five_bytes).has_value());
    MCRS_CHECK(queue.pending_bytes() == 5);
}

void batch_preserves_order_and_respects_byte_limit()
{
    OutboundQueue queue{64};
    const auto first = packet_with_size(4);
    const auto second = packet_with_size(5);
    const auto third = packet_with_size(6);

    MCRS_CHECK(queue.push(first).has_value());
    MCRS_CHECK(queue.push(second).has_value());
    MCRS_CHECK(queue.push(third).has_value());

    std::array<SharedPacket, 3> batch;
    const auto first_count = queue.pop_batch(batch, 9);

    MCRS_CHECK(first_count == 2);
    MCRS_CHECK(batch[0].get() == first.get());
    MCRS_CHECK(batch[1].get() == second.get());
    MCRS_CHECK(queue.pending_bytes() == 6);

    const auto second_count = queue.pop_batch(batch, 9);
    MCRS_CHECK(second_count == 1);
    MCRS_CHECK(batch[0].get() == third.get());
    MCRS_CHECK(queue.empty());
}

void one_large_packet_can_exceed_the_batch_target()
{
    OutboundQueue queue{64};
    const auto packet = packet_with_size(12);
    MCRS_CHECK(queue.push(packet).has_value());

    std::array<SharedPacket, 2> batch;
    const auto count = queue.pop_batch(batch, 8);

    MCRS_CHECK(count == 1);
    MCRS_CHECK(batch[0].get() == packet.get());
    MCRS_CHECK(queue.empty());
}

void batch_respects_destination_capacity()
{
    OutboundQueue queue{64};
    MCRS_CHECK(queue.push(packet_with_size(2)).has_value());
    MCRS_CHECK(queue.push(packet_with_size(2)).has_value());
    MCRS_CHECK(queue.push(packet_with_size(2)).has_value());

    std::array<SharedPacket, 2> batch;
    MCRS_CHECK(queue.pop_batch(batch, 64) == 2);
    MCRS_CHECK(queue.pending_bytes() == 2);
    MCRS_CHECK(queue.pop_batch(batch, 64) == 1);
    MCRS_CHECK(queue.empty());
}

void one_packet_cannot_exceed_the_entire_queue_limit()
{
    OutboundQueue queue{4};
    const auto rejected = queue.push(packet_with_size(5));

    MCRS_CHECK(!rejected.has_value());
    MCRS_CHECK(rejected.error() == OutboundQueueError::packet_too_large);
    MCRS_CHECK(queue.empty());
}

void close_releases_packets_and_rejects_new_work()
{
    OutboundQueue queue{64};
    MCRS_CHECK(queue.push(packet_with_size(8)).has_value());

    queue.close();
    const auto rejected = queue.push(packet_with_size(8));

    MCRS_CHECK(queue.closed());
    MCRS_CHECK(queue.empty());
    MCRS_CHECK(queue.pending_bytes() == 0);
    MCRS_CHECK(!rejected.has_value());
    MCRS_CHECK(rejected.error() == OutboundQueueError::closed);
}
} // namespace

int main()
{
    run_test("immutable packet is shared", one_immutable_packet_can_be_shared_by_multiple_queues);
    run_test("pending byte limit", pending_byte_limit_rejects_a_slow_consumer);
    run_test("batch order and byte limit", batch_preserves_order_and_respects_byte_limit);
    run_test("large packet batch progress", one_large_packet_can_exceed_the_batch_target);
    run_test("batch packet count limit", batch_respects_destination_capacity);
    run_test("single packet size limit", one_packet_cannot_exceed_the_entire_queue_limit);
    run_test("close releases queued packets", close_releases_packets_and_rejects_new_work);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "All outbound queue tests passed\n";
    return 0;
}

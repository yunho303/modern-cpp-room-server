#include "mcrs/network/outbound_queue.hpp"

#include <utility>

namespace mcrs::network
{
SharedPacket make_shared_packet(PacketBuffer packet)
{
    return std::make_shared<const PacketBuffer>(std::move(packet));
}

OutboundQueue::OutboundQueue(std::size_t max_pending_bytes)
    : max_pending_bytes_{max_pending_bytes}
{
}

std::expected<void, OutboundQueueError> OutboundQueue::push(SharedPacket packet)
{
    if (closed_)
    {
        return std::unexpected(OutboundQueueError::closed);
    }

    if (!packet || packet->empty())
    {
        return std::unexpected(OutboundQueueError::invalid_packet);
    }

    if (packet->size() > max_pending_bytes_)
    {
        return std::unexpected(OutboundQueueError::packet_too_large);
    }

    if (packet->size() > max_pending_bytes_ - pending_bytes_)
    {
        return std::unexpected(OutboundQueueError::byte_limit_exceeded);
    }

    packets_.push_back(std::move(packet));
    pending_bytes_ += packets_.back()->size();
    return {};
}

std::size_t OutboundQueue::pop_batch(std::span<SharedPacket> destination,
                                     std::size_t max_batch_bytes)
{
    if (destination.empty() || max_batch_bytes == 0)
    {
        return 0;
    }

    std::size_t packet_count = 0;
    std::size_t batch_bytes = 0;
    while (packet_count < destination.size() && !packets_.empty())
    {
        const auto packet_bytes = packets_.front()->size();
        if (packet_count != 0 &&
            (batch_bytes >= max_batch_bytes || packet_bytes > max_batch_bytes - batch_bytes))
        {
            break;
        }

        destination[packet_count] = std::move(packets_.front());
        packets_.pop_front();
        pending_bytes_ -= packet_bytes;
        batch_bytes += packet_bytes;
        ++packet_count;
    }

    return packet_count;
}

void OutboundQueue::close()
{
    packets_.clear();
    pending_bytes_ = 0;
    closed_ = true;
}

bool OutboundQueue::empty() const noexcept
{
    return packets_.empty();
}

bool OutboundQueue::closed() const noexcept
{
    return closed_;
}

std::size_t OutboundQueue::pending_bytes() const noexcept
{
    return pending_bytes_;
}

std::size_t OutboundQueue::pending_packets() const noexcept
{
    return packets_.size();
}

std::size_t OutboundQueue::max_pending_bytes() const noexcept
{
    return max_pending_bytes_;
}
} // namespace mcrs::network

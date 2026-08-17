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

std::optional<SharedPacket> OutboundQueue::pop()
{
    if (packets_.empty())
    {
        return std::nullopt;
    }

    auto packet = std::move(packets_.front());
    packets_.pop_front();
    pending_bytes_ -= packet->size();
    return packet;
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

std::size_t OutboundQueue::max_pending_bytes() const noexcept
{
    return max_pending_bytes_;
}
} // namespace mcrs::network

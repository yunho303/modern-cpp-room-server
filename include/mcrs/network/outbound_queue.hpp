#pragma once

#include <cstddef>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

namespace mcrs::network
{
using PacketBuffer = std::vector<std::byte>;
using SharedPacket = std::shared_ptr<const PacketBuffer>;

enum class OutboundQueueError
{
    closed,
    invalid_packet,
    packet_too_large,
    byte_limit_exceeded,
};

[[nodiscard]] SharedPacket make_shared_packet(PacketBuffer packet);

// This queue is executor-confined. Producers must post onto the owning Session executor.
class OutboundQueue final
{
public:
    explicit OutboundQueue(std::size_t max_pending_bytes);

    [[nodiscard]] std::expected<void, OutboundQueueError> push(SharedPacket packet);
    [[nodiscard]] std::optional<SharedPacket> pop();

    void close();

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] std::size_t pending_bytes() const noexcept;
    [[nodiscard]] std::size_t max_pending_bytes() const noexcept;

private:
    std::deque<SharedPacket> packets_;
    std::size_t pending_bytes_{};
    std::size_t max_pending_bytes_{};
    bool closed_ = false;
};
} // namespace mcrs::network

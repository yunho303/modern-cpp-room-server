#pragma once

#include "mcrs/network/outbound_queue.hpp"
#include "mcrs/observability/server_metrics.hpp"
#include "mcrs/room/room_event.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace mcrs::network
{
class SessionOutboundEndpoint
{
  public:
    virtual ~SessionOutboundEndpoint() = default;
    virtual void deliver(SharedPacket packet) = 0;
};

class SessionRegistry final
{
  public:
    explicit SessionRegistry(std::shared_ptr<observability::ServerMetrics> metrics = {});
    ~SessionRegistry();
    [[nodiscard]] const std::shared_ptr<observability::ServerMetrics> &metrics() const noexcept;

    [[nodiscard]] bool register_session(room::SessionId session_id, std::weak_ptr<SessionOutboundEndpoint> endpoint);
    void unregister_session(room::SessionId session_id);

    void publish(const room::RoomEvent &event);

    [[nodiscard]] std::size_t connected_count() const;
    [[nodiscard]] std::size_t room_member_count() const;

  private:
    std::shared_ptr<observability::ServerMetrics> metrics_;
    mutable std::mutex mutex_;
    std::unordered_map<room::SessionId, std::weak_ptr<SessionOutboundEndpoint>, room::SessionIdHash> sessions_;
    std::unordered_set<room::SessionId, room::SessionIdHash> room_members_;
};
} // namespace mcrs::network

#include "mcrs/network/session_registry.hpp"

#include "mcrs/network/room_event_encoder.hpp"

#include <concepts>
#include <iostream>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace mcrs::network
{
bool SessionRegistry::register_session(room::SessionId session_id,
                                       std::weak_ptr<SessionOutboundEndpoint> endpoint)
{
    std::lock_guard lock{mutex_};
    return sessions_.try_emplace(session_id, std::move(endpoint)).second;
}

void SessionRegistry::unregister_session(room::SessionId session_id)
{
    std::lock_guard lock{mutex_};
    sessions_.erase(session_id);
    room_members_.erase(session_id);
}

void SessionRegistry::publish(const room::RoomEvent& event)
{
    const auto encoded = encode_room_event(event);
    if (!encoded)
    {
        std::cerr << "failed to encode Room event\n";
        return;
    }

    std::vector<std::shared_ptr<SessionOutboundEndpoint>> recipients;
    {
        std::lock_guard lock{mutex_};

        std::visit(
            [this](const auto& concrete_event)
            {
                using Event = std::remove_cvref_t<decltype(concrete_event)>;
                if constexpr (std::same_as<Event, room::PlayerJoinedEvent>)
                {
                    room_members_.insert(concrete_event.session_id);
                }
                else if constexpr (std::same_as<Event, room::PlayerLeftEvent>)
                {
                    room_members_.erase(concrete_event.session_id);
                }
            },
            event);

        recipients.reserve(room_members_.size());
        for (auto member = room_members_.begin(); member != room_members_.end();)
        {
            const auto session = sessions_.find(*member);
            if (session == sessions_.end())
            {
                member = room_members_.erase(member);
                continue;
            }

            auto endpoint = session->second.lock();
            if (!endpoint)
            {
                sessions_.erase(session);
                member = room_members_.erase(member);
                continue;
            }

            recipients.push_back(std::move(endpoint));
            ++member;
        }
    }

    for (const auto& recipient : recipients)
    {
        recipient->deliver(*encoded);
    }
}

std::size_t SessionRegistry::connected_count() const
{
    std::lock_guard lock{mutex_};
    return sessions_.size();
}

std::size_t SessionRegistry::room_member_count() const
{
    std::lock_guard lock{mutex_};
    return room_members_.size();
}
} // namespace mcrs::network

#include "mcrs/observability/server_metrics.hpp"

#include <ostream>
#include <stdexcept>
#include <utility>

namespace mcrs::observability
{
namespace
{
constexpr auto relaxed = std::memory_order_relaxed;

void update_peak(std::atomic<std::uint64_t> &peak, std::uint64_t value) noexcept
{
    auto previous = peak.load(relaxed);
    while (previous < value && !peak.compare_exchange_weak(previous, value, relaxed))
    {
    }
}

void write_session(std::ostream &output, const SessionSnapshot &s)
{
    output << "{\"session_id\":" << s.session_id << ",\"posted_jobs\":" << s.posted_jobs
           << ",\"posted_bytes\":" << s.posted_bytes << ",\"queued_packets\":" << s.queued_packets
           << ",\"queued_bytes\":" << s.queued_bytes << ",\"inflight_packets\":" << s.inflight_packets
           << ",\"inflight_bytes\":" << s.inflight_bytes << ",\"posted_peak_bytes\":" << s.posted_peak_bytes
           << ",\"queue_peak_bytes\":" << s.queue_peak_bytes << ",\"inflight_peak_bytes\":" << s.inflight_peak_bytes
           << '}';
}
} // namespace

std::shared_ptr<SessionMetrics> ServerMetrics::create_session(std::uint64_t session_id)
{
    auto state = std::make_shared<SessionMetrics>(shared_from_this(), session_id);
    std::lock_guard lock{sessions_mutex_};
    if (!sessions_.emplace(session_id, state).second)
    {
        throw std::invalid_argument{"duplicate metrics session id"};
    }
    state->tracked_ = true;
    return state;
}

void ServerMetrics::retire_session(SessionSnapshot state) noexcept
{
    std::lock_guard lock{sessions_mutex_};
    sessions_.erase(state.session_id);
    closed_[closed_next_] = state;
    closed_next_ = (closed_next_ + 1) % closed_history_capacity;
    if (closed_count_ < closed_history_capacity)
    {
        ++closed_count_;
    }
    else
    {
        ++closed_history_overwritten_;
    }
}

ServerSnapshot ServerMetrics::snapshot() const
{
    ServerSnapshot result{
        .uptime_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at_)
                .count()),
        .room_commands_processed = room_commands_processed_.load(relaxed),
        .room_commands_rejected = room_commands_rejected_.load(relaxed),
        .event_delivery_failures = event_delivery_failures_.load(relaxed),
        .broadcast_delivery_attempts = broadcast_delivery_attempts_.load(relaxed),
        .write_batches_started = write_batches_started_.load(relaxed),
        .write_batches_completed = write_batches_completed_.load(relaxed),
        .write_batches_failed = write_batches_failed_.load(relaxed),
        .write_batches_abandoned = write_batches_abandoned_.load(relaxed),
        .packets_batched = packets_batched_.load(relaxed),
        .max_batch_packets = max_batch_packets_.load(relaxed),
        .bytes_requested = bytes_requested_.load(relaxed),
        .bytes_transferred = bytes_transferred_.load(relaxed),
        .post_handlers_abandoned = post_handlers_abandoned_.load(relaxed),
        .outbound_overflow_closes = outbound_overflow_closes_.load(relaxed),
        .registered_sessions = registered_sessions_.load(relaxed),
        .active_rooms = active_rooms_.load(relaxed),
        .session_queue_peak_bytes = session_queue_peak_bytes_.load(relaxed),
        .closed_history_overwritten = 0,
        .sessions = {},
        .recent_closed_sessions = {},
    };
    // Hold strong references outside the lock: their destructors retire into this same map.
    std::vector<std::shared_ptr<SessionMetrics>> live;
    {
        std::lock_guard lock{sessions_mutex_};
        live.reserve(sessions_.size());
        for (const auto &[id, weak] : sessions_)
        {
            static_cast<void>(id);
            if (auto session = weak.lock())
            {
                live.push_back(std::move(session));
            }
        }
        result.closed_history_overwritten = closed_history_overwritten_;
        result.recent_closed_sessions.reserve(closed_count_);
        const auto first = (closed_next_ + closed_history_capacity - closed_count_) % closed_history_capacity;
        for (std::size_t index = 0; index < closed_count_; ++index)
        {
            result.recent_closed_sessions.push_back(closed_[(first + index) % closed_history_capacity]);
        }
    }
    result.sessions.reserve(live.size());
    for (const auto &session : live)
    {
        result.sessions.push_back(session->snapshot());
    }
    return result;
}

void ServerMetrics::room_opened() noexcept
{
    active_rooms_.fetch_add(1, relaxed);
}
void ServerMetrics::room_closed() noexcept
{
    active_rooms_.fetch_sub(1, relaxed);
}
void ServerMetrics::command_processed(bool rejected) noexcept
{
    room_commands_processed_.fetch_add(1, relaxed);
    if (rejected)
    {
        room_commands_rejected_.fetch_add(1, relaxed);
    }
}
void ServerMetrics::delivery_attempted() noexcept
{
    broadcast_delivery_attempts_.fetch_add(1, relaxed);
}
void ServerMetrics::event_delivery_failed() noexcept
{
    event_delivery_failures_.fetch_add(1, relaxed);
}
void ServerMetrics::session_registered() noexcept
{
    registered_sessions_.fetch_add(1, relaxed);
}
void ServerMetrics::session_unregistered() noexcept
{
    registered_sessions_.fetch_sub(1, relaxed);
}
void ServerMetrics::outbound_overflow() noexcept
{
    outbound_overflow_closes_.fetch_add(1, relaxed);
}

SessionMetrics::SessionMetrics(std::shared_ptr<ServerMetrics> owner, std::uint64_t session_id)
    : owner_{std::move(owner)}, session_id_{session_id}
{
}
SessionMetrics::~SessionMetrics()
{
    if (tracked_)
    {
        owner_->retire_session(snapshot());
    }
}
void SessionMetrics::outbound_overflow() noexcept
{
    owner_->outbound_overflow();
}

SessionSnapshot SessionMetrics::snapshot() const noexcept
{
    return {session_id_,
            posted_jobs_.load(relaxed),
            posted_bytes_.load(relaxed),
            queued_packets_.load(relaxed),
            queued_bytes_.load(relaxed),
            inflight_packets_.load(relaxed),
            inflight_bytes_.load(relaxed),
            posted_peak_bytes_.load(relaxed),
            queue_peak_bytes_.load(relaxed),
            inflight_peak_bytes_.load(relaxed)};
}

void SessionMetrics::queue_changed(std::uint64_t packets, std::uint64_t bytes) noexcept
{
    queued_packets_.store(packets, relaxed);
    queued_bytes_.store(bytes, relaxed);
    update_peak(queue_peak_bytes_, bytes);
    update_peak(owner_->session_queue_peak_bytes_, bytes);
}
void SessionMetrics::post_added(std::uint64_t bytes) noexcept
{
    posted_jobs_.fetch_add(1, relaxed);
    update_peak(posted_peak_bytes_, posted_bytes_.fetch_add(bytes, relaxed) + bytes);
}
void SessionMetrics::post_released(std::uint64_t bytes, bool abandoned) noexcept
{
    posted_jobs_.fetch_sub(1, relaxed);
    posted_bytes_.fetch_sub(bytes, relaxed);
    if (abandoned)
    {
        owner_->post_handlers_abandoned_.fetch_add(1, relaxed);
    }
}
void SessionMetrics::batch_started(std::uint64_t packets, std::uint64_t bytes) noexcept
{
    inflight_packets_.store(packets, relaxed);
    inflight_bytes_.store(bytes, relaxed);
    update_peak(inflight_peak_bytes_, bytes);
    owner_->write_batches_started_.fetch_add(1, relaxed);
    owner_->packets_batched_.fetch_add(packets, relaxed);
    owner_->bytes_requested_.fetch_add(bytes, relaxed);
    update_peak(owner_->max_batch_packets_, packets);
}
void SessionMetrics::batch_finished(std::uint64_t transferred, bool failed, bool abandoned) noexcept
{
    inflight_packets_.store(0, relaxed);
    inflight_bytes_.store(0, relaxed);
    if (abandoned)
    {
        owner_->write_batches_abandoned_.fetch_add(1, relaxed);
        return;
    }
    owner_->bytes_transferred_.fetch_add(transferred, relaxed);
    owner_->write_batches_completed_.fetch_add(1, relaxed);
    if (failed)
    {
        owner_->write_batches_failed_.fetch_add(1, relaxed);
    }
}

PendingDelivery::PendingDelivery(std::shared_ptr<SessionMetrics> metrics, std::uint64_t bytes) noexcept
    : metrics_{std::move(metrics)}, bytes_{bytes}
{
    if (metrics_)
    {
        metrics_->post_added(bytes_);
    }
}
PendingDelivery::PendingDelivery(PendingDelivery &&other) noexcept
    : metrics_{std::move(other.metrics_)}, bytes_{other.bytes_}
{
}
PendingDelivery::~PendingDelivery()
{
    if (metrics_)
    {
        metrics_->post_released(bytes_, true);
    }
}
void PendingDelivery::begin() noexcept
{
    if (auto metrics = std::move(metrics_))
    {
        metrics->post_released(bytes_, false);
    }
}

WriteBatch::WriteBatch(std::shared_ptr<SessionMetrics> metrics, std::uint64_t packets, std::uint64_t bytes) noexcept
    : metrics_{std::move(metrics)}
{
    if (metrics_)
    {
        metrics_->batch_started(packets, bytes);
    }
}
WriteBatch::~WriteBatch()
{
    if (metrics_)
    {
        metrics_->batch_finished(0, false, true);
    }
}
void WriteBatch::complete(std::uint64_t transferred, bool failed) noexcept
{
    if (auto metrics = std::move(metrics_))
    {
        metrics->batch_finished(transferred, failed, false);
    }
}

void write_json(std::ostream &output, const ServerSnapshot &s)
{
    output << "{\"type\":\"server_metrics\",\"scope\":\"process_lifetime\""
           << ",\"uptime_ms\":" << s.uptime_ms << ",\"room_commands_processed\":" << s.room_commands_processed
           << ",\"room_commands_rejected\":" << s.room_commands_rejected
           << ",\"event_delivery_failures\":" << s.event_delivery_failures
           << ",\"broadcast_delivery_attempts\":" << s.broadcast_delivery_attempts
           << ",\"write_batches_started\":" << s.write_batches_started
           << ",\"write_batches_completed\":" << s.write_batches_completed
           << ",\"write_batches_failed\":" << s.write_batches_failed
           << ",\"write_batches_abandoned\":" << s.write_batches_abandoned
           << ",\"packets_batched\":" << s.packets_batched << ",\"max_batch_packets\":" << s.max_batch_packets
           << ",\"bytes_requested\":" << s.bytes_requested << ",\"bytes_transferred\":" << s.bytes_transferred
           << ",\"post_handlers_abandoned\":" << s.post_handlers_abandoned
           << ",\"outbound_overflow_closes\":" << s.outbound_overflow_closes
           << ",\"registered_sessions\":" << s.registered_sessions << ",\"active_rooms\":" << s.active_rooms
           << ",\"session_queue_peak_bytes\":" << s.session_queue_peak_bytes
           << ",\"closed_history_overwritten\":" << s.closed_history_overwritten << ",\"sessions\":[";
    bool first = true;
    for (const auto &session : s.sessions)
    {
        if (!first)
        {
            output << ',';
        }
        first = false;
        write_session(output, session);
    }
    output << "],\"recent_closed_sessions\":[";
    first = true;
    for (const auto &session : s.recent_closed_sessions)
    {
        if (!first)
        {
            output << ',';
        }
        first = false;
        write_session(output, session);
    }
    output << "]}";
}
} // namespace mcrs::observability

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace mcrs::observability
{
struct SessionSnapshot
{
    std::uint64_t session_id{};
    std::uint64_t posted_jobs{};
    std::uint64_t posted_bytes{};
    std::uint64_t queued_packets{};
    std::uint64_t queued_bytes{};
    std::uint64_t inflight_packets{};
    std::uint64_t inflight_bytes{};
    std::uint64_t posted_peak_bytes{};
    std::uint64_t queue_peak_bytes{};
    std::uint64_t inflight_peak_bytes{};
};

struct ServerSnapshot
{
    std::uint64_t uptime_ms{};
    std::uint64_t room_commands_processed{};
    std::uint64_t room_commands_rejected{};
    std::uint64_t event_delivery_failures{};
    std::uint64_t broadcast_delivery_attempts{};
    std::uint64_t write_batches_started{};
    std::uint64_t write_batches_completed{};
    std::uint64_t write_batches_failed{};
    std::uint64_t write_batches_abandoned{};
    std::uint64_t packets_batched{};
    std::uint64_t max_batch_packets{};
    std::uint64_t bytes_requested{};
    std::uint64_t bytes_transferred{};
    std::uint64_t post_handlers_abandoned{};
    std::uint64_t outbound_overflow_closes{};
    std::uint64_t registered_sessions{};
    std::uint64_t active_rooms{};
    std::uint64_t session_queue_peak_bytes{};
    std::uint64_t closed_history_overwritten{};
    std::vector<SessionSnapshot> sessions;
    std::vector<SessionSnapshot> recent_closed_sessions;
};

class SessionMetrics;

// Counters are process-lifetime totals. A live snapshot is not an atomic transaction.
class ServerMetrics final : public std::enable_shared_from_this<ServerMetrics>
{
  public:
    static constexpr std::size_t closed_history_capacity = 128;

    [[nodiscard]] std::shared_ptr<SessionMetrics> create_session(std::uint64_t session_id);
    [[nodiscard]] ServerSnapshot snapshot() const;
    void room_opened() noexcept;
    void room_closed() noexcept;
    void command_processed(bool rejected) noexcept;
    void event_delivery_failed() noexcept;
    void delivery_attempted() noexcept;
    void session_registered() noexcept;
    void session_unregistered() noexcept;
    void outbound_overflow() noexcept;

  private:
    friend class SessionMetrics;
    void retire_session(SessionSnapshot state) noexcept;

    const std::chrono::steady_clock::time_point started_at_ = std::chrono::steady_clock::now();
    std::atomic<std::uint64_t> room_commands_processed_{};
    std::atomic<std::uint64_t> room_commands_rejected_{};
    std::atomic<std::uint64_t> event_delivery_failures_{};
    std::atomic<std::uint64_t> broadcast_delivery_attempts_{};
    std::atomic<std::uint64_t> write_batches_started_{};
    std::atomic<std::uint64_t> write_batches_completed_{};
    std::atomic<std::uint64_t> write_batches_failed_{};
    std::atomic<std::uint64_t> write_batches_abandoned_{};
    std::atomic<std::uint64_t> packets_batched_{};
    std::atomic<std::uint64_t> max_batch_packets_{};
    std::atomic<std::uint64_t> bytes_requested_{};
    std::atomic<std::uint64_t> bytes_transferred_{};
    std::atomic<std::uint64_t> post_handlers_abandoned_{};
    std::atomic<std::uint64_t> outbound_overflow_closes_{};
    std::atomic<std::uint64_t> registered_sessions_{};
    std::atomic<std::uint64_t> active_rooms_{};
    std::atomic<std::uint64_t> session_queue_peak_bytes_{};
    mutable std::mutex sessions_mutex_;
    std::unordered_map<std::uint64_t, std::weak_ptr<SessionMetrics>> sessions_;
    std::array<SessionSnapshot, closed_history_capacity> closed_{};
    std::size_t closed_count_{};
    std::size_t closed_next_{};
    std::uint64_t closed_history_overwritten_{};
};

class SessionMetrics final
{
  public:
    SessionMetrics(std::shared_ptr<ServerMetrics> owner, std::uint64_t session_id);
    ~SessionMetrics();
    [[nodiscard]] SessionSnapshot snapshot() const noexcept;
    void queue_changed(std::uint64_t packets, std::uint64_t bytes) noexcept;
    void outbound_overflow() noexcept;

  private:
    friend class ServerMetrics;
    friend class PendingDelivery;
    friend class WriteBatch;
    void post_added(std::uint64_t bytes) noexcept;
    void post_released(std::uint64_t bytes, bool abandoned) noexcept;
    void batch_started(std::uint64_t packets, std::uint64_t bytes) noexcept;
    void batch_finished(std::uint64_t transferred, bool failed, bool abandoned) noexcept;

    std::shared_ptr<ServerMetrics> owner_;
    std::uint64_t session_id_;
    bool tracked_ = false;
    std::atomic<std::uint64_t> posted_jobs_{};
    std::atomic<std::uint64_t> posted_bytes_{};
    std::atomic<std::uint64_t> queued_packets_{};
    std::atomic<std::uint64_t> queued_bytes_{};
    std::atomic<std::uint64_t> inflight_packets_{};
    std::atomic<std::uint64_t> inflight_bytes_{};
    std::atomic<std::uint64_t> posted_peak_bytes_{};
    std::atomic<std::uint64_t> queue_peak_bytes_{};
    std::atomic<std::uint64_t> inflight_peak_bytes_{};
};

// Travels with the posted handler: destruction without begin also releases the gauges.
class PendingDelivery final
{
  public:
    PendingDelivery(std::shared_ptr<SessionMetrics> metrics, std::uint64_t bytes) noexcept;
    ~PendingDelivery();
    PendingDelivery(PendingDelivery &&other) noexcept;
    PendingDelivery(const PendingDelivery &) = delete;
    PendingDelivery &operator=(const PendingDelivery &) = delete;
    PendingDelivery &operator=(PendingDelivery &&) = delete;
    void begin() noexcept;

  private:
    std::shared_ptr<SessionMetrics> metrics_;
    std::uint64_t bytes_;
};

// Remains in the writer coroutine until completion, exception, or frame destruction.
class WriteBatch final
{
  public:
    WriteBatch(std::shared_ptr<SessionMetrics> metrics, std::uint64_t packets, std::uint64_t bytes) noexcept;
    ~WriteBatch();
    WriteBatch(const WriteBatch &) = delete;
    WriteBatch &operator=(const WriteBatch &) = delete;
    void complete(std::uint64_t transferred, bool failed) noexcept;

  private:
    std::shared_ptr<SessionMetrics> metrics_;
};

void write_json(std::ostream &output, const ServerSnapshot &state);
} // namespace mcrs::observability

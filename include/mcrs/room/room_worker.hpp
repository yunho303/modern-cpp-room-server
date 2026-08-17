#pragma once

#include "mcrs/concurrency/closeable_queue.hpp"
#include "mcrs/room/room.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace mcrs::room
{
struct RoomWorkerSummary
{
    std::vector<PlayerSnapshot> players;
    std::size_t processed_commands{};
    std::size_t rejected_commands{};
};

class RoomWorker final
{
public:
    RoomWorker();
    ~RoomWorker();

    RoomWorker(const RoomWorker&) = delete;
    RoomWorker& operator=(const RoomWorker&) = delete;
    RoomWorker(RoomWorker&&) = delete;
    RoomWorker& operator=(RoomWorker&&) = delete;

    [[nodiscard]] std::expected<void, concurrency::QueuePushError> submit(RoomCommand command);

    // stop drains commands accepted before close and then transfers a stable snapshot to the caller.
    RoomWorkerSummary stop();

private:
    void run(std::stop_token stop_token);

    Room room_;
    concurrency::CloseableQueue<RoomCommand> commands_;
    std::size_t processed_commands_{};
    std::size_t rejected_commands_{};
    std::optional<RoomWorkerSummary> stopped_summary_;
    std::jthread worker_;
};
} // namespace mcrs::room

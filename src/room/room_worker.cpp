#include "mcrs/room/room_worker.hpp"

#include <utility>

namespace mcrs::room
{
RoomWorker::RoomWorker()
    : worker_{[this](std::stop_token stop_token)
              { run(stop_token); }}
{
}

RoomWorker::~RoomWorker()
{
    static_cast<void>(stop());
}

std::expected<void, concurrency::QueuePushError> RoomWorker::submit(RoomCommand command)
{
    return commands_.push(std::move(command));
}

RoomWorkerSummary RoomWorker::stop()
{
    if (stopped_summary_)
    {
        return *stopped_summary_;
    }

    commands_.close();
    worker_.request_stop();
    if (worker_.joinable())
    {
        worker_.join();
    }

    stopped_summary_ = RoomWorkerSummary{
        .players = room_.snapshot(),
        .processed_commands = processed_commands_,
        .rejected_commands = rejected_commands_,
    };
    return *stopped_summary_;
}

void RoomWorker::run(std::stop_token stop_token)
{
    while (auto command = commands_.wait_pop(stop_token))
    {
        const auto result = room_.apply(*command);
        ++processed_commands_;

        if (!result)
        {
            ++rejected_commands_;
        }
    }
}
} // namespace mcrs::room

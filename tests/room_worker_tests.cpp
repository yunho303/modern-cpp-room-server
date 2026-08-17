#include "mcrs/concurrency/closeable_queue.hpp"
#include "mcrs/room/room_worker.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <source_location>
#include <stop_token>
#include <string_view>
#include <thread>

namespace
{
using namespace std::chrono_literals;
using mcrs::concurrency::CloseableQueue;
using mcrs::concurrency::QueuePushError;
using namespace mcrs::room;

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

void queue_preserves_fifo_order_and_drains_after_close()
{
    CloseableQueue<int> queue;

    MCRS_CHECK(queue.push(10).has_value());
    MCRS_CHECK(queue.push(20).has_value());
    queue.close();

    std::stop_source stop_source;
    const auto first = queue.wait_pop(stop_source.get_token());
    const auto second = queue.wait_pop(stop_source.get_token());
    const auto finished = queue.wait_pop(stop_source.get_token());
    const auto rejected = queue.push(30);

    MCRS_CHECK(first == 10);
    MCRS_CHECK(second == 20);
    MCRS_CHECK(!finished.has_value());
    MCRS_CHECK(!rejected.has_value());
    MCRS_CHECK(rejected.error() == QueuePushError::closed);
}

void stop_token_wakes_an_idle_consumer()
{
    CloseableQueue<int> queue;
    std::promise<bool> wait_finished;
    auto wait_result = wait_finished.get_future();

    std::jthread consumer{[&](std::stop_token stop_token)
                          { wait_finished.set_value(!queue.wait_pop(stop_token).has_value()); }};

    consumer.request_stop();
    MCRS_CHECK(wait_result.wait_for(1s) == std::future_status::ready);
    MCRS_CHECK(wait_result.get());
}

void room_worker_applies_commands_in_submission_order()
{
    RoomWorker worker;
    constexpr SessionId session_id{201};
    constexpr GridPosition destination{8, -5};

    MCRS_CHECK(worker.submit(JoinCommand{.session_id = session_id}).has_value());
    MCRS_CHECK(worker.submit(MoveCommand{.session_id = session_id, .destination = destination}).has_value());

    const auto summary = worker.stop();

    MCRS_CHECK(summary.processed_commands == 2);
    MCRS_CHECK(summary.rejected_commands == 0);
    MCRS_CHECK(summary.players.size() == 1);
    MCRS_CHECK(summary.players.front().session_id == session_id);
    MCRS_CHECK(summary.players.front().position == destination);

    const auto rejected_after_stop = worker.submit(LeaveCommand{
        .session_id = session_id,
        .reason = DisconnectReason::server_shutdown,
    });
    MCRS_CHECK(!rejected_after_stop.has_value());
    MCRS_CHECK(rejected_after_stop.error() == QueuePushError::closed);
}

void room_worker_counts_rejected_domain_commands()
{
    RoomWorker worker;
    constexpr SessionId session_id{202};

    MCRS_CHECK(worker.submit(JoinCommand{.session_id = session_id}).has_value());
    MCRS_CHECK(worker.submit(JoinCommand{.session_id = session_id}).has_value());
    MCRS_CHECK(worker.submit(LeaveCommand{
        .session_id = session_id,
        .reason = DisconnectReason::client_closed,
    }).has_value());

    const auto summary = worker.stop();

    MCRS_CHECK(summary.processed_commands == 3);
    MCRS_CHECK(summary.rejected_commands == 1);
    MCRS_CHECK(summary.players.empty());
}
} // namespace

int main()
{
    run_test("queue drains in FIFO order", queue_preserves_fifo_order_and_drains_after_close);
    run_test("stop token wakes consumer", stop_token_wakes_an_idle_consumer);
    run_test("Room Worker preserves command order", room_worker_applies_commands_in_submission_order);
    run_test("Room Worker records rejected commands", room_worker_counts_rejected_domain_commands);

    if (failure_count != 0)
    {
        std::cerr << failure_count << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "All Room Worker tests passed\n";
    return 0;
}

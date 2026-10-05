#include "mcrs/observability/server_metrics.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using namespace mcrs::observability;

void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error{message};
    }
}

void post_lifetime()
{
    auto metrics = std::make_shared<ServerMetrics>();
    auto session = metrics->create_session(1);
    {
        asio::io_context context;
        asio::post(context, [pending = PendingDelivery{session, 100}]() mutable { pending.begin(); });
        check(session->snapshot().posted_jobs == 1, "post must be counted before handler execution");
        check(session->snapshot().posted_bytes == 100, "posted payload retained");
        context.stop();
        check(session->snapshot().posted_jobs == 1, "stop alone does not destroy queued handlers");
        context.restart();
        context.run();
    }
    check(session->snapshot().posted_bytes == 0, "executed handler releases reservation");
    check(metrics->snapshot().post_handlers_abandoned == 0, "executed handler is not abandoned");
    {
        asio::io_context context;
        asio::post(context, [pending = PendingDelivery{session, 200}]() mutable { pending.begin(); });
    }
    check(session->snapshot().posted_jobs == 0, "context destruction releases queued handler");
    check(session->snapshot().posted_bytes == 0, "destroyed handler releases bytes");
    check(metrics->snapshot().post_handlers_abandoned == 1, "unexecuted handler counted once");
    try
    {
        PendingDelivery pending{session, 300};
        throw std::runtime_error{"simulated submission failure"};
    }
    catch (const std::runtime_error &)
    {
    }
    check(session->snapshot().posted_bytes == 0, "submission exception releases reservation");
    check(metrics->snapshot().post_handlers_abandoned == 2, "submission failure recorded");
}

asio::awaitable<void> suspended_batch(std::shared_ptr<SessionMetrics> session)
{
    WriteBatch batch{session, 2, 100};
    asio::steady_timer timer{co_await asio::this_coro::executor};
    timer.expires_after(std::chrono::hours{1});
    co_await timer.async_wait(asio::use_awaitable);
    batch.complete(100, false);
}

void batch_completion_and_frame_destruction()
{
    auto metrics = std::make_shared<ServerMetrics>();
    auto session = metrics->create_session(2);
    {
        WriteBatch batch{session, 3, 240};
        check(session->snapshot().inflight_bytes == 240, "in-flight bytes retained");
        batch.complete(90, true);
        batch.complete(90, true); // Once-only release even on duplicate cleanup.
    }
    check(session->snapshot().inflight_bytes == 0, "partial failure releases in-flight state");
    {
        asio::io_context context;
        asio::co_spawn(context, suspended_batch(session), asio::detached);
        context.poll();
        check(session->snapshot().inflight_bytes == 100, "coroutine suspended with batch");
    }
    const auto result = metrics->snapshot();
    check(session->snapshot().inflight_bytes == 0, "destroyed coroutine releases in-flight state");
    check(result.write_batches_started == 2 && result.write_batches_completed == 1,
          "started and completed counted separately");
    check(result.write_batches_failed == 1 && result.write_batches_abandoned == 1,
          "callback failure and abandoned frame distinguished");
    check(result.bytes_requested == 340 && result.bytes_transferred == 90, "partial bytes preserved");
    check(result.packets_batched == 5 && result.max_batch_packets == 3, "batch distribution totals");
}

void concurrent_producers()
{
    auto metrics = std::make_shared<ServerMetrics>();
    auto session = metrics->create_session(3);
    {
        std::vector<std::jthread> producers;
        for (int thread = 0; thread < 4; ++thread)
        {
            producers.emplace_back([session] {
                for (int iteration = 0; iteration < 1000; ++iteration)
                {
                    PendingDelivery pending{session, 22};
                    if (iteration % 2 == 0)
                    {
                        pending.begin();
                    }
                }
            });
        }
    }
    check(session->snapshot().posted_jobs == 0 && session->snapshot().posted_bytes == 0,
          "concurrent reservations balance after join");
    check(metrics->snapshot().post_handlers_abandoned == 2000, "concurrent abandoned total");
}

void bounded_history_and_duplicate_ids()
{
    auto metrics = std::make_shared<ServerMetrics>();
    auto first = metrics->create_session(1);
    bool rejected = false;
    try
    {
        static_cast<void>(metrics->create_session(1));
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    check(rejected && metrics->snapshot().sessions.size() == 1, "duplicate must not remove live metrics");
    first.reset();
    for (std::uint64_t id = 2; id <= 129; ++id)
    {
        auto session = metrics->create_session(id);
        session->queue_changed(1, id);
        session->queue_changed(0, 0);
    }
    const auto result = metrics->snapshot();
    check(result.sessions.empty(), "metrics must not retain closed sessions");
    check(result.recent_closed_sessions.size() == ServerMetrics::closed_history_capacity, "closed history is bounded");
    check(result.closed_history_overwritten == 1, "history loss is explicit");
    check(result.recent_closed_sessions.front().session_id == 2 &&
              result.recent_closed_sessions.back().session_id == 129,
          "history is chronological");
    check(result.recent_closed_sessions.back().queued_bytes == 0 &&
              result.recent_closed_sessions.back().queue_peak_bytes == 129,
          "high-water survives cleanup");
    check(result.session_queue_peak_bytes == 129, "process high-water includes closed sessions");
}
} // namespace

int main()
{
    try
    {
        post_lifetime();
        batch_completion_and_frame_destruction();
        concurrent_producers();
        bounded_history_and_duplicate_ids();
        std::cout << "[PASS] post, coroutine, partial write, concurrent metrics, bounded history\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}

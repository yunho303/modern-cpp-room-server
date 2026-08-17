#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace mcrs::concurrency
{
enum class QueuePushError
{
    closed,
};

template <typename T>
class CloseableQueue final
{
public:
    [[nodiscard]] std::expected<void, QueuePushError> push(T value)
    {
        {
            std::lock_guard lock{mutex_};
            if (closed_)
            {
                return std::unexpected(QueuePushError::closed);
            }

            values_.push_back(std::move(value));
        }

        value_available_.notify_one();
        return {};
    }

    [[nodiscard]] std::optional<T> wait_pop(std::stop_token stop_token)
    {
        std::unique_lock lock{mutex_};
        value_available_.wait(lock, stop_token, [this]
                              { return closed_ || !values_.empty(); });

        if (values_.empty())
        {
            return std::nullopt;
        }

        T value = std::move(values_.front());
        values_.pop_front();
        return value;
    }

    void close()
    {
        {
            std::lock_guard lock{mutex_};
            closed_ = true;
        }

        value_available_.notify_all();
    }

    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard lock{mutex_};
        return values_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable_any value_available_;
    std::deque<T> values_;
    bool closed_ = false;
};
} // namespace mcrs::concurrency

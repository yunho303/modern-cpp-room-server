#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace mcrs::network
{
enum class ReceiveBufferError
{
    buffer_limit_exceeded,
};

class ReceiveBuffer final
{
public:
    explicit ReceiveBuffer(std::size_t max_buffered_bytes, std::size_t initial_capacity = 4U * 1024U);

    // append and consume can invalidate a previously returned readable view.
    [[nodiscard]] std::expected<void, ReceiveBufferError> append(std::span<const std::byte> bytes);
    void consume(std::size_t byte_count) noexcept;

    [[nodiscard]] std::span<const std::byte> readable_bytes() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t max_size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    void compact() noexcept;

    std::vector<std::byte> storage_;
    std::size_t max_buffered_bytes_{};
    std::size_t read_offset_{};
};
} // namespace mcrs::network

#include "mcrs/network/receive_buffer.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace mcrs::network
{
ReceiveBuffer::ReceiveBuffer(std::size_t max_buffered_bytes, std::size_t initial_capacity)
    : max_buffered_bytes_{max_buffered_bytes}
{
    storage_.reserve(std::min(initial_capacity, max_buffered_bytes_));
}

std::expected<void, ReceiveBufferError> ReceiveBuffer::append(std::span<const std::byte> bytes)
{
    if (bytes.empty())
    {
        return {};
    }

    if (bytes.size() > max_buffered_bytes_ - size())
    {
        return std::unexpected(ReceiveBufferError::buffer_limit_exceeded);
    }

    const auto unused_tail_capacity = storage_.capacity() - storage_.size();
    if (read_offset_ != 0 && unused_tail_capacity < bytes.size())
    {
        compact();
    }

    storage_.insert(storage_.end(), bytes.begin(), bytes.end());
    return {};
}

void ReceiveBuffer::consume(std::size_t byte_count) noexcept
{
    assert(byte_count <= size());

    read_offset_ += byte_count;
    if (read_offset_ == storage_.size())
    {
        storage_.clear();
        read_offset_ = 0;
    }
}

std::span<const std::byte> ReceiveBuffer::readable_bytes() const noexcept
{
    return std::span{storage_}.subspan(read_offset_);
}

std::size_t ReceiveBuffer::size() const noexcept
{
    return storage_.size() - read_offset_;
}

std::size_t ReceiveBuffer::max_size() const noexcept
{
    return max_buffered_bytes_;
}

bool ReceiveBuffer::empty() const noexcept
{
    return size() == 0;
}

void ReceiveBuffer::compact() noexcept
{
    const auto readable_size = size();
    std::memmove(storage_.data(), storage_.data() + read_offset_, readable_size);
    storage_.resize(readable_size);
    read_offset_ = 0;
}
} // namespace mcrs::network

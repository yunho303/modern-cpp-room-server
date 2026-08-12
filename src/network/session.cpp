#include "mcrs/network/session.hpp"

#include "mcrs/network/receive_buffer.hpp"
#include "mcrs/protocol/packet_codec.hpp"

#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <utility>

namespace mcrs::network
{
namespace
{
constexpr std::size_t tcp_read_chunk_size = 4U * 1024U;
constexpr std::size_t max_session_buffer_size =
    protocol::wire_header_size + protocol::max_payload_size + tcp_read_chunk_size;

class Session final
{
public:
    explicit Session(asio::ip::tcp::socket socket)
        : socket_{std::move(socket)}, receive_buffer_{max_session_buffer_size}
    {
    }

    [[nodiscard]] asio::awaitable<void> run()
    {
        std::array<std::byte, tcp_read_chunk_size> read_chunk{};

        for (;;)
        {
            asio::error_code read_error;
            const auto bytes_read = co_await socket_.async_read_some(
                asio::buffer(read_chunk), asio::redirect_error(asio::use_awaitable, read_error));

            if (read_error == asio::error::eof || read_error == asio::error::connection_reset)
            {
                co_return;
            }

            if (read_error)
            {
                std::cerr << "session read failed: " << read_error.message() << '\n';
                co_return;
            }

            const auto appended = receive_buffer_.append(std::span{read_chunk}.first(bytes_read));
            if (!appended)
            {
                std::cerr << "session receive buffer limit exceeded\n";
                co_return;
            }

            const bool should_continue = co_await process_packets_and_send_responses();
            if (!should_continue)
            {
                co_return;
            }
        }
    }

private:
    [[nodiscard]] asio::awaitable<bool> process_packets_and_send_responses()
    {
        while (!receive_buffer_.empty())
        {
            const auto decoded = protocol::decode_one(receive_buffer_.readable_bytes());
            if (!decoded)
            {
                if (protocol::is_incomplete(decoded.error()))
                {
                    co_return true;
                }

                std::cerr << "session rejected packet: " << protocol::to_string(decoded.error()) << '\n';
                co_return false;
            }

            if (decoded->header.type == protocol::PacketType::ping)
            {
                auto response = protocol::encode_packet(protocol::PacketType::ping, decoded->payload);
                if (!response)
                {
                    co_return false;
                }

                receive_buffer_.consume(decoded->consumed_bytes);

                asio::error_code write_error;
                co_await asio::async_write(
                    socket_, asio::buffer(response->data(), response->size()),
                    asio::redirect_error(asio::use_awaitable, write_error));

                if (write_error)
                {
                    std::cerr << "session write failed: " << write_error.message() << '\n';
                    co_return false;
                }
            }
            else
            {
                receive_buffer_.consume(decoded->consumed_bytes);
            }
        }

        co_return true;
    }

    asio::ip::tcp::socket socket_;
    ReceiveBuffer receive_buffer_;
};
} // namespace

asio::awaitable<void> run_session(asio::ip::tcp::socket socket)
{
    Session session{std::move(socket)};
    co_await session.run();
}
} // namespace mcrs::network

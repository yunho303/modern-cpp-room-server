#include "mcrs/network/session.hpp"

#include "mcrs/network/receive_buffer.hpp"
#include "mcrs/protocol/gameplay_payload.hpp"
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
#include <string_view>
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
    Session(asio::ip::tcp::socket socket, room::SessionId session_id, room::RoomWorker& room_worker)
        : socket_{std::move(socket)},
          receive_buffer_{max_session_buffer_size},
          session_id_{session_id},
          room_worker_{room_worker}
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
                leave_room_if_joined(room::DisconnectReason::client_closed);
                co_return;
            }

            if (read_error)
            {
                std::cerr << "session read failed: " << read_error.message() << '\n';
                leave_room_if_joined(room::DisconnectReason::io_error);
                co_return;
            }

            const auto appended = receive_buffer_.append(std::span{read_chunk}.first(bytes_read));
            if (!appended)
            {
                std::cerr << "session receive buffer limit exceeded\n";
                leave_room_if_joined(room::DisconnectReason::protocol_error);
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
                leave_room_if_joined(room::DisconnectReason::protocol_error);
                co_return false;
            }

            switch (decoded->header.type)
            {
            case protocol::PacketType::ping:
            {
                auto response = protocol::encode_packet(protocol::PacketType::ping, decoded->payload);
                if (!response)
                {
                    leave_room_if_joined(room::DisconnectReason::protocol_error);
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
                    leave_room_if_joined(room::DisconnectReason::io_error);
                    co_return false;
                }
                break;
            }
            case protocol::PacketType::join_room:
            {
                if (!protocol::validate_empty_payload(decoded->payload))
                {
                    co_return reject_protocol("join packet payload must be empty");
                }

                if (joined_room_)
                {
                    co_return reject_protocol("session attempted to join twice");
                }

                if (!submit_room_command(room::JoinCommand{.session_id = session_id_}))
                {
                    co_return false;
                }

                joined_room_ = true;
                receive_buffer_.consume(decoded->consumed_bytes);
                break;
            }
            case protocol::PacketType::move:
            {
                if (!joined_room_)
                {
                    co_return reject_protocol("session attempted to move before joining");
                }

                const auto movement = protocol::decode_move_payload(decoded->payload);
                if (!movement)
                {
                    co_return reject_protocol(protocol::to_string(movement.error()));
                }

                if (!submit_room_command(room::MoveCommand{
                        .session_id = session_id_,
                        .destination = room::GridPosition{.x = movement->x, .y = movement->y},
                    }))
                {
                    leave_room_if_joined(room::DisconnectReason::server_shutdown);
                    co_return false;
                }

                receive_buffer_.consume(decoded->consumed_bytes);
                break;
            }
            case protocol::PacketType::leave_room:
            {
                if (!protocol::validate_empty_payload(decoded->payload))
                {
                    co_return reject_protocol("leave packet payload must be empty");
                }

                if (!joined_room_)
                {
                    co_return reject_protocol("session attempted to leave before joining");
                }

                leave_room_if_joined(room::DisconnectReason::left_room);
                receive_buffer_.consume(decoded->consumed_bytes);
                break;
            }
            case protocol::PacketType::attack:
                co_return reject_protocol("attack packets are not implemented");
            }
        }

        co_return true;
    }

    [[nodiscard]] bool submit_room_command(room::RoomCommand command)
    {
        const auto submitted = room_worker_.submit(std::move(command));
        if (!submitted)
        {
            std::cerr << "Room Worker no longer accepts commands\n";
            return false;
        }

        return true;
    }

    void leave_room_if_joined(room::DisconnectReason reason)
    {
        if (!std::exchange(joined_room_, false))
        {
            return;
        }

        static_cast<void>(submit_room_command(room::LeaveCommand{
            .session_id = session_id_,
            .reason = reason,
        }));
    }

    [[nodiscard]] bool reject_protocol(std::string_view reason)
    {
        std::cerr << "session rejected gameplay packet: " << reason << '\n';
        leave_room_if_joined(room::DisconnectReason::protocol_error);
        return false;
    }

    asio::ip::tcp::socket socket_;
    ReceiveBuffer receive_buffer_;
    room::SessionId session_id_;
    room::RoomWorker& room_worker_;
    bool joined_room_ = false;
};
} // namespace

asio::awaitable<void> run_session(asio::ip::tcp::socket socket, room::SessionId session_id,
                                  room::RoomWorker& room_worker)
{
    Session session{std::move(socket), session_id, room_worker};
    co_await session.run();
}
} // namespace mcrs::network

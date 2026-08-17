#include "mcrs/network/session.hpp"

#include "mcrs/network/outbound_queue.hpp"
#include "mcrs/network/receive_buffer.hpp"
#include "mcrs/protocol/gameplay_payload.hpp"
#include "mcrs/protocol/packet_codec.hpp"

#include <asio/any_io_executor.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/post.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
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
constexpr std::size_t max_session_outbound_bytes = 256U * 1024U;

class Session final : public SessionOutboundEndpoint,
                      public std::enable_shared_from_this<Session>
{
public:
    Session(asio::ip::tcp::socket socket, room::SessionId session_id, room::RoomWorker& room_worker)
        : executor_{socket.get_executor()},
          socket_{std::move(socket)},
          receive_buffer_{max_session_buffer_size},
          outbound_queue_{max_session_outbound_bytes},
          session_id_{session_id},
          room_worker_{room_worker}
    {
    }

    void deliver(SharedPacket packet) override
    {
        asio::post(executor_,
                   [self = shared_from_this(), packet = std::move(packet)]() mutable
                   {
                       static_cast<void>(self->enqueue_outbound(std::move(packet)));
                   });
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
                stop_session(room::DisconnectReason::client_closed);
                co_return;
            }

            if (read_error == asio::error::operation_aborted && stopped_)
            {
                co_return;
            }

            if (read_error)
            {
                std::cerr << "session read failed: " << read_error.message() << '\n';
                stop_session(room::DisconnectReason::io_error);
                co_return;
            }

            const auto appended = receive_buffer_.append(std::span{read_chunk}.first(bytes_read));
            if (!appended)
            {
                std::cerr << "session receive buffer limit exceeded\n";
                stop_session(room::DisconnectReason::protocol_error);
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
                stop_session(room::DisconnectReason::protocol_error);
                co_return false;
            }

            switch (decoded->header.type)
            {
            case protocol::PacketType::ping:
            {
                auto response = protocol::encode_packet(protocol::PacketType::ping, decoded->payload);
                if (!response)
                {
                    stop_session(room::DisconnectReason::protocol_error);
                    co_return false;
                }

                receive_buffer_.consume(decoded->consumed_bytes);
                if (!enqueue_outbound(make_shared_packet(std::move(*response))))
                {
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
                    stop_session(room::DisconnectReason::server_shutdown);
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
                    stop_session(room::DisconnectReason::server_shutdown);
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
            case protocol::PacketType::player_joined:
            case protocol::PacketType::player_moved:
            case protocol::PacketType::player_left:
                co_return reject_protocol("client sent a server-only packet type");
            }
        }

        co_return true;
    }

    [[nodiscard]] bool enqueue_outbound(SharedPacket packet)
    {
        if (stopped_)
        {
            return false;
        }

        const auto pushed = outbound_queue_.push(std::move(packet));
        if (!pushed)
        {
            if (pushed.error() == OutboundQueueError::byte_limit_exceeded)
            {
                std::cerr << "session outbound byte limit exceeded\n";
                stop_session(room::DisconnectReason::outbound_overflow);
            }
            else
            {
                std::cerr << "session rejected outbound packet\n";
                stop_session(room::DisconnectReason::io_error);
            }
            return false;
        }

        if (!write_in_progress_)
        {
            write_in_progress_ = true;
            auto self = shared_from_this();
            asio::co_spawn(
                executor_, write_queued_packets(),
                [self = std::move(self)](std::exception_ptr exception)
                {
                    if (!exception)
                    {
                        return;
                    }

                    try
                    {
                        std::rethrow_exception(exception);
                    }
                    catch (const std::exception& error)
                    {
                        std::cerr << "session writer stopped by exception: " << error.what() << '\n';
                    }
                    catch (...)
                    {
                        std::cerr << "session writer stopped by an unknown exception\n";
                    }

                    self->stop_session(room::DisconnectReason::io_error);
                });
        }

        return true;
    }

    [[nodiscard]] asio::awaitable<void> write_queued_packets()
    {
        while (!stopped_)
        {
            auto packet = outbound_queue_.pop();
            if (!packet)
            {
                write_in_progress_ = false;
                co_return;
            }

            asio::error_code write_error;
            co_await asio::async_write(
                socket_, asio::buffer(**packet),
                asio::redirect_error(asio::use_awaitable, write_error));

            if (write_error)
            {
                write_in_progress_ = false;
                if (write_error != asio::error::operation_aborted || !stopped_)
                {
                    std::cerr << "session write failed: " << write_error.message() << '\n';
                }
                stop_session(room::DisconnectReason::io_error);
                co_return;
            }
        }

        write_in_progress_ = false;
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
        stop_session(room::DisconnectReason::protocol_error);
        return false;
    }

    void stop_session(room::DisconnectReason reason)
    {
        if (std::exchange(stopped_, true))
        {
            return;
        }

        leave_room_if_joined(reason);
        outbound_queue_.close();

        asio::error_code ignored;
        socket_.cancel(ignored);
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
    }

    asio::any_io_executor executor_;
    asio::ip::tcp::socket socket_;
    ReceiveBuffer receive_buffer_;
    OutboundQueue outbound_queue_;
    room::SessionId session_id_;
    room::RoomWorker& room_worker_;
    bool joined_room_ = false;
    bool write_in_progress_ = false;
    bool stopped_ = false;
};
} // namespace

asio::awaitable<void> run_session(asio::ip::tcp::socket socket, room::SessionId session_id,
                                  room::RoomWorker& room_worker,
                                  SessionRegistry& session_registry)
{
    auto session = std::make_shared<Session>(std::move(socket), session_id, room_worker);
    if (!session_registry.register_session(session_id, session))
    {
        co_return;
    }

    try
    {
        co_await session->run();
    }
    catch (...)
    {
        session_registry.unregister_session(session_id);
        throw;
    }

    session_registry.unregister_session(session_id);
}
} // namespace mcrs::network

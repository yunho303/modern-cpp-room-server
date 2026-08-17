#include "mcrs/network/session.hpp"
#include "mcrs/protocol/gameplay_payload.hpp"
#include "mcrs/protocol/packet_codec.hpp"
#include "mcrs/room/room_worker.hpp"

#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using asio::ip::tcp;
using namespace mcrs::protocol;
using ClientScenario = asio::awaitable<bool> (*)(std::uint16_t);

asio::awaitable<void> accept_one(tcp::acceptor acceptor, mcrs::room::RoomWorker& room_worker)
{
    auto socket = co_await acceptor.async_accept(asio::use_awaitable);
    co_await mcrs::network::run_session(std::move(socket), mcrs::room::SessionId{1}, room_worker);
}

asio::awaitable<tcp::socket> connect_client(std::uint16_t port)
{
    const auto executor = co_await asio::this_coro::executor;
    tcp::socket socket{executor};
    co_await socket.async_connect({asio::ip::address_v4::loopback(), port}, asio::use_awaitable);
    co_return std::move(socket);
}

asio::awaitable<bool> ping_round_trip(std::uint16_t port)
{
    auto socket = co_await connect_client(port);
    constexpr std::array payload{std::byte{0xCA}, std::byte{0xFE}};
    const auto request = encode_packet(PacketType::ping, payload);
    if (!request)
    {
        co_return false;
    }

    co_await asio::async_write(socket, asio::buffer(*request), asio::use_awaitable);

    std::vector<std::byte> response(request->size());
    co_await asio::async_read(socket, asio::buffer(response), asio::use_awaitable);

    const auto decoded = decode_one(response);
    socket.close();
    co_return decoded && decoded->header.type == PacketType::ping &&
              std::ranges::equal(decoded->payload, payload);
}

asio::awaitable<bool> ping_sent_in_two_writes_round_trip(std::uint16_t port)
{
    auto socket = co_await connect_client(port);
    constexpr std::array payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    const auto request = encode_packet(PacketType::ping, payload);
    if (!request)
    {
        co_return false;
    }

    constexpr std::size_t first_part_size = wire_header_size - 1;
    co_await asio::async_write(socket, asio::buffer(request->data(), first_part_size), asio::use_awaitable);

    asio::steady_timer split_delay{co_await asio::this_coro::executor};
    split_delay.expires_after(std::chrono::milliseconds{10});
    co_await split_delay.async_wait(asio::use_awaitable);

    co_await asio::async_write(
        socket, asio::buffer(request->data() + first_part_size, request->size() - first_part_size),
        asio::use_awaitable);

    std::vector<std::byte> response(request->size());
    co_await asio::async_read(socket, asio::buffer(response), asio::use_awaitable);

    const auto decoded = decode_one(response);
    socket.close();
    co_return decoded && decoded->header.type == PacketType::ping &&
              std::ranges::equal(decoded->payload, payload);
}

asio::awaitable<bool> concatenated_pings_round_trip(std::uint16_t port)
{
    auto socket = co_await connect_client(port);
    constexpr std::array first_payload{std::byte{0x01}};
    constexpr std::array second_payload{std::byte{0x02}, std::byte{0x03}};
    const auto first = encode_packet(PacketType::ping, first_payload);
    const auto second = encode_packet(PacketType::ping, second_payload);
    if (!first || !second)
    {
        co_return false;
    }

    std::vector<std::byte> requests;
    requests.reserve(first->size() + second->size());
    requests.insert(requests.end(), first->begin(), first->end());
    requests.insert(requests.end(), second->begin(), second->end());
    co_await asio::async_write(socket, asio::buffer(requests), asio::use_awaitable);

    std::vector<std::byte> responses(requests.size());
    co_await asio::async_read(socket, asio::buffer(responses), asio::use_awaitable);

    const auto decoded_first = decode_one(responses);
    if (!decoded_first)
    {
        co_return false;
    }

    const auto decoded_second = decode_one(std::span{responses}.subspan(decoded_first->consumed_bytes));
    socket.close();
    co_return decoded_first->header.type == PacketType::ping &&
              std::ranges::equal(decoded_first->payload, first_payload) && decoded_second &&
              decoded_second->header.type == PacketType::ping &&
              std::ranges::equal(decoded_second->payload, second_payload);
}

asio::awaitable<bool> invalid_packet_closes_connection(std::uint16_t port)
{
    auto socket = co_await connect_client(port);
    constexpr std::array<std::byte, wire_header_size> unknown_type_header{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x7F}, std::byte{0xFF},
    };

    co_await asio::async_write(socket, asio::buffer(unknown_type_header), asio::use_awaitable);

    std::array<std::byte, 1> response{};
    asio::error_code read_error;
    co_await socket.async_read_some(
        asio::buffer(response), asio::redirect_error(asio::use_awaitable, read_error));

    socket.close();
    co_return read_error == asio::error::eof || read_error == asio::error::connection_reset;
}

asio::awaitable<bool> joined_session_moves_and_leaves_on_disconnect(std::uint16_t port)
{
    auto socket = co_await connect_client(port);
    const auto join = encode_packet(PacketType::join_room, {});
    constexpr MovePayload movement{.x = -17, .y = 29};
    const auto movement_bytes = encode_move_payload(movement);
    const auto move = encode_packet(PacketType::move, movement_bytes);
    if (!join || !move)
    {
        co_return false;
    }

    std::vector<std::byte> requests;
    requests.reserve(join->size() + move->size());
    requests.insert(requests.end(), join->begin(), join->end());
    requests.insert(requests.end(), move->begin(), move->end());

    co_await asio::async_write(socket, asio::buffer(requests), asio::use_awaitable);
    socket.shutdown(tcp::socket::shutdown_send);
    socket.close();
    co_return true;
}

bool run_scenario(std::string_view name, ClientScenario scenario,
                  std::size_t expected_processed_commands = 0,
                  std::size_t expected_rejected_commands = 0,
                  std::size_t expected_players = 0)
{
    asio::io_context context{1};
    tcp::acceptor acceptor{context, {tcp::v4(), 0}};
    const auto port = acceptor.local_endpoint().port();
    mcrs::room::RoomWorker room_worker;

    bool server_completed = false;
    bool client_completed = false;
    bool scenario_succeeded = false;
    bool coroutine_failed = false;

    asio::co_spawn(context, accept_one(std::move(acceptor), room_worker),
                   [&server_completed, &coroutine_failed](std::exception_ptr exception) {
                       server_completed = true;
                       coroutine_failed = coroutine_failed || exception != nullptr;
                   });

    asio::co_spawn(context, scenario(port),
                   [&client_completed, &scenario_succeeded, &coroutine_failed](std::exception_ptr exception,
                                                                               bool succeeded) {
                       client_completed = true;
                       scenario_succeeded = succeeded;
                       coroutine_failed = coroutine_failed || exception != nullptr;
                   });

    context.run();
    const auto room_summary = room_worker.stop();

    const bool room_succeeded = room_summary.processed_commands == expected_processed_commands &&
                                room_summary.rejected_commands == expected_rejected_commands &&
                                room_summary.players.size() == expected_players;
    const bool passed = server_completed && client_completed && scenario_succeeded && !coroutine_failed &&
                        room_succeeded;
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << '\n';
    return passed;
}
} // namespace

int main()
{
    const bool ping_passed = run_scenario("coroutine TCP ping round trip", ping_round_trip);
    const bool two_writes_passed = run_scenario("ping sent in two client writes", ping_sent_in_two_writes_round_trip);
    const bool concatenated_passed = run_scenario("concatenated TCP packets", concatenated_pings_round_trip);
    const bool invalid_passed = run_scenario("invalid packet closes connection", invalid_packet_closes_connection);
    const bool room_commands_passed = run_scenario(
        "join and move reach Room Worker before disconnect cleanup",
        joined_session_moves_and_leaves_on_disconnect,
        3);

    return ping_passed && two_writes_passed && concatenated_passed && invalid_passed &&
                   room_commands_passed
               ? 0
               : 1;
}

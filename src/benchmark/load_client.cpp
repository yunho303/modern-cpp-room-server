#include "mcrs/benchmark/load_client.hpp"

#include "mcrs/network/receive_buffer.hpp"
#include "mcrs/protocol/gameplay_payload.hpp"
#include "mcrs/protocol/packet_codec.hpp"

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcrs::benchmark
{
namespace
{
using Clock = std::chrono::steady_clock;
using asio::ip::tcp;

constexpr std::size_t read_chunk_size = 4U * 1024U;
constexpr std::size_t max_receive_buffer_size =
    protocol::wire_header_size + protocol::max_payload_size + read_chunk_size;

struct ServerEvent
{
    protocol::PacketType type;
    std::optional<protocol::PlayerStatePayload> player_state;
};

struct MoveSample
{
    Clock::time_point started_at;
    Clock::time_point completed_at;
};

class RunState final
{
public:
    explicit RunState(const LoadTestConfig& config)
        : client_count_{config.client_count},
          expected_samples_{config.client_count * config.measured_moves_per_client}
    {
        latencies_.reserve(expected_samples_);
    }

    void client_ready() noexcept
    {
        ++ready_clients_;
    }

    void client_warmed_up() noexcept
    {
        ++warmed_up_clients_;
    }

    void client_reached_measurement_barrier() noexcept
    {
        ++measurement_barrier_clients_;
    }

    void client_completed() noexcept
    {
        ++completed_clients_;
    }

    [[nodiscard]] bool all_clients_ready() const noexcept
    {
        return ready_clients_ == client_count_;
    }

    [[nodiscard]] bool all_clients_warmed_up() const noexcept
    {
        return warmed_up_clients_ == client_count_;
    }

    [[nodiscard]] bool all_clients_reached_measurement_barrier() const noexcept
    {
        return measurement_barrier_clients_ == client_count_;
    }

    [[nodiscard]] bool all_clients_completed() const noexcept
    {
        return completed_clients_ == client_count_;
    }

    void record(MoveSample sample)
    {
        if (!measurement_started_at_ || sample.started_at < *measurement_started_at_)
        {
            measurement_started_at_ = sample.started_at;
        }
        if (!measurement_completed_at_ || sample.completed_at > *measurement_completed_at_)
        {
            measurement_completed_at_ = sample.completed_at;
        }

        latencies_.push_back(
            std::chrono::duration_cast<Latency>(sample.completed_at - sample.started_at));
    }

    void fail(std::string message)
    {
        if (!failure_)
        {
            failure_ = std::move(message);
        }
    }

    [[nodiscard]] bool failed() const noexcept
    {
        return failure_.has_value();
    }

    [[nodiscard]] const std::optional<std::string>& failure() const noexcept
    {
        return failure_;
    }

    [[nodiscard]] std::span<const Latency> latencies() const noexcept
    {
        return latencies_;
    }

    [[nodiscard]] std::optional<Latency> elapsed() const noexcept
    {
        if (!measurement_started_at_ || !measurement_completed_at_ ||
            latencies_.size() != expected_samples_)
        {
            return std::nullopt;
        }

        return std::chrono::duration_cast<Latency>(
            *measurement_completed_at_ - *measurement_started_at_);
    }

private:
    std::size_t client_count_{};
    std::size_t expected_samples_{};
    std::size_t ready_clients_{};
    std::size_t warmed_up_clients_{};
    std::size_t measurement_barrier_clients_{};
    std::size_t completed_clients_{};
    std::vector<Latency> latencies_;
    std::optional<Clock::time_point> measurement_started_at_;
    std::optional<Clock::time_point> measurement_completed_at_;
    std::optional<std::string> failure_;
};

[[nodiscard]] std::runtime_error network_error(std::string_view operation,
                                               const asio::error_code& error)
{
    return std::runtime_error{std::string{operation} + ": " + error.message()};
}

asio::awaitable<void> send_packet(tcp::socket& socket, protocol::PacketType type,
                                  std::span<const std::byte> payload)
{
    auto packet = protocol::encode_packet(type, payload);
    if (!packet)
    {
        throw std::runtime_error{std::string{"encode packet: "} +
                                 std::string{protocol::to_string(packet.error())}};
    }

    asio::error_code error;
    co_await asio::async_write(socket, asio::buffer(*packet),
                               asio::redirect_error(asio::use_awaitable, error));
    if (error)
    {
        throw network_error("write packet", error);
    }
}

asio::awaitable<ServerEvent>
read_next_server_event(tcp::socket& socket, network::ReceiveBuffer& receive_buffer)
{
    std::array<std::byte, read_chunk_size> read_chunk{};

    for (;;)
    {
        while (!receive_buffer.empty())
        {
            const auto decoded = protocol::decode_one(receive_buffer.readable_bytes());
            if (!decoded)
            {
                if (protocol::is_incomplete(decoded.error()))
                {
                    break;
                }

                throw std::runtime_error{std::string{"decode packet: "} +
                                         std::string{protocol::to_string(decoded.error())}};
            }

            const auto type = decoded->header.type;
            if (type == protocol::PacketType::player_joined ||
                type == protocol::PacketType::player_moved)
            {
                const auto player = protocol::decode_player_state_payload(decoded->payload);
                if (!player)
                {
                    throw std::runtime_error{
                        std::string{"decode player state: "} +
                        std::string{protocol::to_string(player.error())}};
                }

                receive_buffer.consume(decoded->consumed_bytes);
                co_return ServerEvent{.type = type, .player_state = *player};
            }

            if (type == protocol::PacketType::player_left)
            {
                const auto player = protocol::decode_player_left_payload(decoded->payload);
                if (!player)
                {
                    throw std::runtime_error{
                        std::string{"decode player left: "} +
                        std::string{protocol::to_string(player.error())}};
                }

                receive_buffer.consume(decoded->consumed_bytes);
                co_return ServerEvent{.type = type, .player_state = std::nullopt};
            }

            receive_buffer.consume(decoded->consumed_bytes);
        }

        asio::error_code error;
        const auto bytes_read = co_await socket.async_read_some(
            asio::buffer(read_chunk), asio::redirect_error(asio::use_awaitable, error));
        if (error)
        {
            throw network_error("read packet", error);
        }

        const auto appended = receive_buffer.append(std::span{read_chunk}.first(bytes_read));
        if (!appended)
        {
            throw std::runtime_error{"receive buffer limit exceeded"};
        }
    }
}

asio::awaitable<void> wait_for_room_entry(tcp::socket& socket,
                                          network::ReceiveBuffer& receive_buffer)
{
    for (;;)
    {
        const auto event = co_await read_next_server_event(socket, receive_buffer);
        if (event.type == protocol::PacketType::player_joined)
        {
            co_return;
        }
    }
}

asio::awaitable<void> wait_for_move_event(tcp::socket& socket,
                                          network::ReceiveBuffer& receive_buffer,
                                          std::int32_t client_coordinate,
                                          std::int32_t sequence_coordinate)
{
    for (;;)
    {
        const auto event = co_await read_next_server_event(socket, receive_buffer);
        if (event.type == protocol::PacketType::player_moved && event.player_state &&
            event.player_state->x == client_coordinate &&
            event.player_state->y == sequence_coordinate)
        {
            co_return;
        }
    }
}

asio::awaitable<MoveSample> perform_move(tcp::socket& socket,
                                         network::ReceiveBuffer& receive_buffer,
                                         std::int32_t client_coordinate,
                                         std::int32_t sequence_coordinate)
{
    const auto payload = protocol::encode_move_payload(protocol::MovePayload{
        .x = client_coordinate,
        .y = sequence_coordinate,
    });

    const auto started_at = Clock::now();
    co_await send_packet(socket, protocol::PacketType::move, payload);
    co_await wait_for_move_event(socket, receive_buffer, client_coordinate,
                                 sequence_coordinate);
    co_return MoveSample{.started_at = started_at, .completed_at = Clock::now()};
}

template <typename Predicate>
asio::awaitable<void> wait_for_gate(RunState& state, Predicate predicate)
{
    const auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer{executor};

    while (!predicate())
    {
        if (state.failed())
        {
            throw std::runtime_error{"another load client failed"};
        }

        timer.expires_after(std::chrono::milliseconds{1});
        co_await timer.async_wait(asio::use_awaitable);
    }
}

asio::awaitable<void> run_bot(tcp::endpoint endpoint, std::size_t client_index,
                              const LoadTestConfig& config, RunState& state)
{
    const auto executor = co_await asio::this_coro::executor;
    tcp::socket socket{executor};
    network::ReceiveBuffer receive_buffer{max_receive_buffer_size};

    asio::error_code connect_error;
    co_await socket.async_connect(
        endpoint, asio::redirect_error(asio::use_awaitable, connect_error));
    if (connect_error)
    {
        throw network_error("connect", connect_error);
    }

    co_await send_packet(socket, protocol::PacketType::join_room, {});
    co_await wait_for_room_entry(socket, receive_buffer);
    state.client_ready();
    co_await wait_for_gate(state, [&state]
                           { return state.all_clients_ready(); });

    const auto client_coordinate = static_cast<std::int32_t>(client_index + 1U);
    for (std::size_t sequence = 0; sequence < config.warmup_moves_per_client; ++sequence)
    {
        static_cast<void>(co_await perform_move(
            socket, receive_buffer, client_coordinate, static_cast<std::int32_t>(sequence + 1U)));
    }

    state.client_warmed_up();
    co_await wait_for_gate(state, [&state]
                           { return state.all_clients_warmed_up(); });

    constexpr std::int32_t barrier_coordinate = 0;
    if (client_index == 0U)
    {
        static_cast<void>(co_await perform_move(socket, receive_buffer, barrier_coordinate,
                                                barrier_coordinate));
    }
    else
    {
        co_await wait_for_move_event(socket, receive_buffer, barrier_coordinate,
                                     barrier_coordinate);
    }

    state.client_reached_measurement_barrier();
    co_await wait_for_gate(state, [&state]
                           { return state.all_clients_reached_measurement_barrier(); });

    for (std::size_t sequence = 0; sequence < config.measured_moves_per_client; ++sequence)
    {
        const auto wire_sequence = config.warmup_moves_per_client + sequence + 1U;
        state.record(co_await perform_move(socket, receive_buffer, client_coordinate,
                                           static_cast<std::int32_t>(wire_sequence)));
    }

    state.client_completed();
    co_await wait_for_gate(state, [&state]
                           { return state.all_clients_completed(); });

    std::size_t observed_leaves = 0;
    while (observed_leaves < client_index)
    {
        const auto event = co_await read_next_server_event(socket, receive_buffer);
        if (event.type == protocol::PacketType::player_left)
        {
            ++observed_leaves;
        }
    }

    co_await send_packet(socket, protocol::PacketType::leave_room, {});
    asio::error_code ignored;
    socket.shutdown(tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
}

[[nodiscard]] std::optional<std::string> validate_config(const LoadTestConfig& config)
{
    if (config.client_count == 0U)
    {
        return "client count must be greater than zero";
    }
    if (config.port == 0U)
    {
        return "port must be greater than zero";
    }
    if (config.measured_moves_per_client == 0U)
    {
        return "measured moves per client must be greater than zero";
    }
    if (config.client_count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
    {
        return "client count exceeds coordinate range";
    }
    if (config.warmup_moves_per_client >
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) -
            config.measured_moves_per_client)
    {
        return "total moves per client exceeds coordinate range";
    }
    if (config.client_count > std::numeric_limits<std::size_t>::max() /
                                  config.measured_moves_per_client)
    {
        return "sample count is too large";
    }
    return std::nullopt;
}
} // namespace

std::expected<LoadTestResult, std::string> run_load_test(const LoadTestConfig& config)
{
    if (const auto error = validate_config(config))
    {
        return std::unexpected(*error);
    }

    asio::error_code address_error;
    const auto address = asio::ip::make_address(config.host, address_error);
    if (address_error)
    {
        return std::unexpected("host must be a numeric IPv4 or IPv6 address");
    }

    asio::io_context context{1};
    RunState state{config};
    const tcp::endpoint endpoint{address, config.port};

    for (std::size_t client_index = 0; client_index < config.client_count; ++client_index)
    {
        asio::co_spawn(
            context, run_bot(endpoint, client_index, config, state),
            [&state, client_index](std::exception_ptr exception)
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
                    state.fail("client " + std::to_string(client_index + 1U) +
                               " failed: " + error.what());
                }
                catch (...)
                {
                    state.fail("client " + std::to_string(client_index + 1U) +
                               " failed with an unknown exception");
                }
            });
    }

    context.run();

    if (state.failure())
    {
        return std::unexpected(*state.failure());
    }

    const auto elapsed = state.elapsed();
    if (!elapsed)
    {
        return std::unexpected("load test completed without the expected samples");
    }

    const auto latency = summarize_latencies(state.latencies());
    const auto throughput = calculate_throughput(state.latencies().size(), *elapsed);
    if (!latency || !throughput)
    {
        return std::unexpected("failed to summarize load test metrics");
    }

    return LoadTestResult{
        .config = config,
        .latency = *latency,
        .elapsed = *elapsed,
        .commands_per_second = *throughput,
        .estimated_deliveries_per_second =
            *throughput * static_cast<double>(config.client_count),
    };
}
} // namespace mcrs::benchmark

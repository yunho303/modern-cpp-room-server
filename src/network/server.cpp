#include "mcrs/network/server.hpp"

#include "mcrs/network/session.hpp"

#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/system_error.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <exception>
#include <iostream>
#include <utility>

namespace mcrs::network
{
namespace
{
void report_session_completion(std::exception_ptr exception)
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
        std::cerr << "session stopped by exception: " << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "session stopped by an unknown exception\n";
    }
}
} // namespace

asio::awaitable<void> run_server(std::uint16_t port, room::RoomWorker& room_worker)
{
    const auto executor = co_await asio::this_coro::executor;
    asio::ip::tcp::acceptor acceptor{executor, {asio::ip::tcp::v4(), port}};
    std::uint64_t next_session_id = 1;
    std::cout << "mcrs_server listening on 0.0.0.0:" << port << '\n';

    for (;;)
    {
        asio::error_code accept_error;
        auto socket = co_await acceptor.async_accept(asio::redirect_error(asio::use_awaitable, accept_error));

        if (accept_error == asio::error::operation_aborted)
        {
            co_return;
        }

        if (accept_error)
        {
            throw asio::system_error{accept_error};
        }

        const room::SessionId session_id{next_session_id++};
        asio::co_spawn(executor, run_session(std::move(socket), session_id, room_worker),
                       report_session_completion);
    }
}
} // namespace mcrs::network

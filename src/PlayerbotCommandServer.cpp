/*
 * Copyright (C) 2016+
 * AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license.
 * You may redistribute it and/or modify it under version 2 of the License,
 * or (at your option), any later version.
 */

#include "PlayerbotCommandServer.h"

#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/bind/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>

#include "IoContext.h"
#include "Playerbots.h"

using boost::asio::ip::tcp;
using boost::asio::io_context;
namespace asio = boost::asio;
using namespace boost::placeholders;

// Alias for a shared pointer to a TCP socket
using socket_ptr = std::shared_ptr<tcp::socket>;

// Forward declaration of the Session class
class Session;

// Thread pool configuration
const std::size_t THREAD_POOL_SIZE = std::max(4u, std::thread::hardware_concurrency());

// Session class to handle individual client connections
class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(socket_ptr socket, asio::io_context& io_context)
        : socket_(std::move(socket)),
          strand_(asio::make_strand(io_context)),
          buffer_(),
          request_()
    {
    }

    void start()
    {
        doRead();
    }

private:
    void doRead()
    {
        auto self(shared_from_this());
        asio::async_read_until(*socket_, asio::dynamic_buffer(buffer_), '\n',
            asio::bind_executor(strand_,
                [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred)
                {
                    if (!ec)
                    {
                        std::string line(buffer_.substr(0, bytes_transferred - 1)); // Exclude '\n'
                        buffer_.erase(0, bytes_transferred);
                        handleCommand(line);
                        doRead();
                    }
                    else if (ec == asio::error::eof)
                    {
                        LOG_INFO("playerbots", "Connection closed by peer.");
                        doClose();
                    }
                    else
                    {
                        LOG_ERROR("playerbots", "Read error: {}", ec.message());
                        doClose();
                    }
                }));
    }

    void handleCommand(const std::string& command)
    {
        try
        {
            // Process the command (Assuming HandleRemoteCommand is thread-safe)
            std::string response = sRandomPlayerbotMgr->HandleRemoteCommand(command) + "\n";
            doWrite(response);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("playerbots", "Command handling error: {}", e.what());
            doClose();
        }
    }

    void doWrite(const std::string& message)
    {
        auto self(shared_from_this());
        asio::async_write(*socket_, asio::buffer(message),
            asio::bind_executor(strand_,
                [this, self](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/)
                {
                    if (ec)
                    {
                        LOG_ERROR("playerbots", "Write error: {}", ec.message());
                        doClose();
                    }
                }));
    }

    void doClose()
    {
        boost::system::error_code ec;
        socket_->shutdown(tcp::socket::shutdown_both, ec);
        socket_->close(ec);
        if (ec)
        {
            LOG_ERROR("playerbots", "Error closing socket: {}", ec.message());
        }
    }

    socket_ptr socket_;
    asio::strand<asio::io_context::executor_type> strand_;
    std::string buffer_;
    std::string request_;
};

// Server class to accept incoming connections
class Server
{
public:
    Server(asio::io_context& io_context, short port)
        : io_context_(io_context),
          acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
          thread_pool_(THREAD_POOL_SIZE)
    {
        LOG_INFO("playerbots", "Starting Playerbots Command Server on port {}", port);
        doAccept();
    }

    ~Server()
    {
        thread_pool_.join();
    }

private:
    void doAccept()
    {
        acceptor_.async_accept(
            boost::asio::make_strand(io_context_),
            boost::bind(&Server::handleAccept, this, asio::placeholders::error, asio::placeholders::socket));
    }

    void handleAccept(const boost::system::error_code& ec, tcp::socket socket)
    {
        if (!ec)
        {
            auto socket_ptr = std::make_shared<tcp::socket>(std::move(socket));
            std::make_shared<Session>(socket_ptr, io_context_)->start();
        }
        else
        {
            LOG_ERROR("playerbots", "Accept error: {}", ec.message());
        }

        // Continue accepting new connections
        doAccept();
    }

    asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    boost::asio::thread_pool thread_pool_;
};

// Function to run the server
void Run()
{
    if (!sPlayerbotAIConfig->commandServerPort)
    {
        return;
    }

    try
    {
        asio::io_context io_context;

        // Create server instance
        Server server(io_context, sPlayerbotAIConfig->commandServerPort);

        // Run the I/O service on a separate thread pool
        std::vector<std::thread> threads;
        const std::size_t thread_count = THREAD_POOL_SIZE;

        for (std::size_t i = 0; i < thread_count; ++i)
        {
            threads.emplace_back([&io_context]()
            {
                io_context.run();
            });
        }

        // Wait for all threads to finish
        for (auto& t : threads)
        {
            if (t.joinable())
                t.join();
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("playerbots", "Server error: {}", e.what());
    }
}

// Start function to launch the server in a detached thread
void PlayerbotCommandServer::Start()
{
    std::thread serverThread(Run);
    serverThread.detach();
}

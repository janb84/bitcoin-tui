// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_TESTS_UTIL_PATH_SERVER_H
#define BITCOIN_TUI_TESTS_UTIL_PATH_SERVER_H

#include "interfaces/init.h"
#include "ipc/capnp/init.capnp.h"
#include "ipc/capnp/init.capnp.proxy.h"

#include <mp/proxy-io.h>

#include <atomic>
#include <cstdio>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace test_util {

// Spin up an in-process Init server reachable via a real unix socket
// path, so IpcClient's public constructor can connect to it. Takes any
// interfaces::Init implementation, so tests can plug in v31-flavoured
// or v30-rejecting variants. The returned RAII object joins the loop
// thread and unlinks the socket on destruction.
class PathServer
{
public:
    explicit PathServer(std::shared_ptr<interfaces::Init> init)
        : m_init(std::move(init))
    {
        m_path = "/tmp/btui-test-ipc-" + std::to_string(::getpid()) + "-" +
                 std::to_string(s_counter++) + ".sock";
        ::unlink(m_path.c_str());

        m_listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(m_listen_fd >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", m_path.c_str());
        REQUIRE(::bind(m_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(m_listen_fd, 4) == 0);

        std::promise<mp::EventLoop*> ready;
        m_loop_thread = std::thread([&] {
            mp::EventLoop loop("test-ipc-server", [](mp::LogMessage msg) {
                if (msg.level == mp::Log::Raise) {
                    throw std::runtime_error(msg.message);
                }
            });
            ready.set_value(&loop);
            loop.loop();
        });
        m_loop = ready.get_future().get();

        // Run an accept loop on a dedicated thread that pushes each
        // accepted fd into ServeStream on the loop thread.
        m_accept_thread = std::thread([this] {
            while (!m_stop.load()) {
                int conn = ::accept(m_listen_fd, nullptr, nullptr);
                if (conn < 0) return;
                m_loop->sync([&] {
                    mp::ServeStream<ipc::capnp::messages::Init>(*m_loop, conn, *m_init);
                });
            }
        });
    }

    ~PathServer()
    {
        m_stop = true;
        if (m_listen_fd >= 0) {
            ::shutdown(m_listen_fd, SHUT_RDWR);
            ::close(m_listen_fd);
            m_listen_fd = -1;
        }
        if (m_accept_thread.joinable()) m_accept_thread.join();
        if (m_loop_thread.joinable()) m_loop_thread.join();
        ::unlink(m_path.c_str());
    }

    const std::string& path() const { return m_path; }

private:
    std::shared_ptr<interfaces::Init> m_init;
    std::string m_path;
    int m_listen_fd{-1};
    std::atomic<bool> m_stop{false};
    mp::EventLoop* m_loop{nullptr};
    std::thread m_loop_thread;
    std::thread m_accept_thread;
    static inline std::atomic<unsigned> s_counter{0};
};

} // namespace test_util

#endif // BITCOIN_TUI_TESTS_UTIL_PATH_SERVER_H

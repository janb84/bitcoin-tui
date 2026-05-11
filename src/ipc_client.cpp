// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ipc_client.hpp"

#include "interfaces/init.h"
#include "interfaces/mining.h"
#include "ipc/capnp/init.capnp.h"
#include "ipc/capnp/init.capnp.proxy.h"
#include "ipc/capnp/mining.capnp.h"
#include "ipc/capnp/mining.capnp.proxy.h"

#include <mp/proxy-io.h>
#include <mp/util.h>

#include <cerrno>
#include <cstring>
#include <future>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {

// Open and connect to a unix domain socket. Returns the fd on success,
// throws on failure.
int ConnectUnix(const std::string& path)
{
    if (path.size() >= sizeof(sockaddr_un{}.sun_path)) {
        throw std::runtime_error("IPC socket path too long: " + path);
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string{"socket(AF_UNIX) failed: "} + std::strerror(errno));
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const int e = errno;
        ::close(fd);
        throw std::runtime_error("connect(" + path + ") failed: " + std::strerror(e));
    }
    return fd;
}

// Sink for libmultiprocess log messages. Drop everything except thrown
// exceptions, which we surface as runtime_error so they propagate through
// the EventLoop::sync() boundary instead of getting swallowed.
void LogPrint(mp::LogMessage msg)
{
    if (msg.level == mp::Log::Raise) {
        throw std::runtime_error(msg.message);
    }
}

} // namespace

struct IpcClient::Impl {
    // The event loop runs on its own thread; libmultiprocess methods are
    // dispatched to that thread via loop->sync().
    std::thread loop_thread;
    mp::EventLoop* loop{nullptr};

    // Owns the connection to the bitcoin-node socket; tied to `init`.
    std::unique_ptr<interfaces::Init> init;
    // Eagerly created in the ctor as part of the version probe; reused
    // later for typed Mining accessors.
    std::unique_ptr<interfaces::Mining> mining;

    explicit Impl(const std::string& socket_path)
    {
        // Connect the unix socket *before* spinning up the EventLoop, so a
        // missing/refused socket throws cleanly without leaving the loop
        // thread parked on its m_post_fd read (which would only be woken
        // when an EventLoopRef's destructor fires — and none ever would).
        const int fd = ConnectUnix(socket_path);

        std::promise<mp::EventLoop*> ready;
        loop_thread = std::thread([&] {
            mp::EventLoop owned("bitcoin-tui-ipc", LogPrint);
            ready.set_value(&owned);
            owned.loop();
        });
        loop = ready.get_future().get();
        try {
            init = mp::ConnectStream<ipc::capnp::messages::Init>(*loop, fd);
        } catch (...) {
            // ConnectStream took ownership of fd before throwing, or never
            // ran; either way we still need to drain the loop.
            shutdown_loop();
            throw;
        }
        // Probe the server's wire version by eagerly fetching the Mining
        // capability. v30.x exposes Mining at Init.makeMining ordinal 2
        // and we call ordinal 3, which does not exist on v30 and faults at
        // the wire level with `MethodNotImplemented; methodId = 3`. v31.0+
        // accepts the call. Round-tripping init->makeMining() is enough on
        // its own to detect the mismatch — no follow-up probe on the
        // returned Mining capability is needed.
        //
        // Surface any failure as a friendly runtime_error so the caller
        // (main.cpp) can log it and fall back to time-based polling over
        // JSON-RPC.
        try {
            mining = init->makeMining();
            if (!mining) {
                throw std::runtime_error("server did not return a Mining capability");
            }
        } catch (const std::exception& e) {
            // Tear down on the loop thread before joining.
            mining.reset();
            init.reset();
            shutdown_loop();
            throw std::runtime_error(
                std::string{"bitcoin-node IPC server is older than v31.0 "
                            "(or otherwise incompatible): "} + e.what());
        }
    }

    ~Impl()
    {
        // Destroy clients on the loop thread before shutting it down.
        mining.reset();
        init.reset();
        shutdown_loop();
    }

    void shutdown_loop()
    {
        if (loop_thread.joinable()) loop_thread.join();
        loop = nullptr;
    }
};

IpcClient::IpcClient(const std::string& socket_path)
    : m_impl(std::make_unique<Impl>(socket_path)) {}

IpcClient::~IpcClient() = default;

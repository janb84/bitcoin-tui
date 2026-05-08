// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ipc_client.hpp"

#include "interfaces/init.h"
#include "interfaces/mining.h"
#include "interfaces/rpc.h"
#include "ipc/capnp/init.capnp.h"
#include "ipc/capnp/init.capnp.proxy.h"
#include "ipc/capnp/mining.capnp.h"
#include "ipc/capnp/mining.capnp.proxy.h"
#include "ipc/capnp/rpc.capnp.h"
#include "ipc/capnp/rpc.capnp.proxy.h"

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

// Open and connect to a unix domain socket. Returns the fd on success, throws
// on failure.
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

    // Owns the connection to the bitcoin-node socket; tied to `init` lifetime.
    std::unique_ptr<interfaces::Init> init;
    // Lazily created on first call; persists until shutdown.
    std::unique_ptr<interfaces::Rpc> rpc;
    std::unique_ptr<interfaces::Mining> mining;

    Impl(const std::string& socket_path)
    {
        std::promise<mp::EventLoop*> ready;
        loop_thread = std::thread([&] {
            mp::EventLoop owned("bitcoin-tui-ipc", LogPrint);
            ready.set_value(&owned);
            owned.loop();
        });
        loop = ready.get_future().get();
        try {
            const int fd = ConnectUnix(socket_path);
            init = mp::ConnectStream<ipc::capnp::messages::Init>(*loop, fd);
        } catch (...) {
            // Tear the loop down so loop_thread can join, then rethrow.
            shutdown_loop();
            throw;
        }
    }

    ~Impl()
    {
        // Destroy clients on the loop thread before shutting it down.
        mining.reset();
        rpc.reset();
        init.reset();
        shutdown_loop();
    }

    void shutdown_loop()
    {
        if (loop) {
            // Posting a no-op shuts the loop down once it returns; the loop
            // exits when there are no more outstanding async tasks.
            // mp::EventLoop's destructor (via the worker thread) handles
            // the actual exit; we just join.
        }
        if (loop_thread.joinable()) loop_thread.join();
        loop = nullptr;
    }

    interfaces::Rpc& get_rpc()
    {
        if (!rpc) {
            rpc = init->makeRpc();
            if (!rpc) throw std::runtime_error("bitcoin-node did not return an Rpc interface");
        }
        return *rpc;
    }

    interfaces::Mining* get_mining()
    {
        if (!mining) {
            try {
                mining = init->makeMining();
            } catch (...) {
                mining.reset();
            }
        }
        return mining.get();
    }
};

IpcClient::IpcClient(const std::string& socket_path)
    : m_impl(std::make_unique<Impl>(socket_path)) {}

IpcClient::~IpcClient() = default;

static json do_call(interfaces::Rpc& rpc,
                    const std::string& uri,
                    const std::string& method,
                    const json& params)
{
    json req = {
        {"method", method},
        {"params", params},
        {"id", 1},
    };
    const std::string response_text = rpc.executeRpc(req.dump(), uri, /*user=*/"");
    json response = json::parse(response_text);
    if (response.contains("error") && !response["error"].is_null()) {
        const auto& err = response["error"];
        std::string msg = "IPC RPC error";
        if (err.is_object() && err.contains("message")) {
            msg += ": " + err["message"].get<std::string>();
        } else {
            msg += ": " + err.dump();
        }
        throw std::runtime_error(msg);
    }
    return response.contains("result") ? response["result"] : json{};
}

json IpcClient::call(const std::string& method, const json& params)
{
    return do_call(m_impl->get_rpc(), "/", method, params);
}

json IpcClient::call_wallet(const std::string& wallet,
                            const std::string& method,
                            const json& params)
{
    return do_call(m_impl->get_rpc(), "/wallet/" + wallet, method, params);
}

interfaces::Mining* IpcClient::mining()
{
    return m_impl->get_mining();
}

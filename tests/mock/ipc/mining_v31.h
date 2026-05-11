// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_TESTS_MOCK_IPC_MINING_V31_H
#define BITCOIN_TUI_TESTS_MOCK_IPC_MINING_V31_H

#include "interfaces/mining.h"
#include "interfaces/types.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace mock_ipc {

// Mock that behaves like a v31.0+ Mining interface: interrupt() is a no-op,
// waitTipChanged returns a configurable hash (or blocks until interrupt).
class MiningV31 : public interfaces::Mining
{
public:
    explicit MiningV31(interfaces::uint256 tip = {}) : m_tip(tip) {}

    bool isTestChain() override            { return true; }
    bool isInitialBlockDownload() override { return false; }

    std::optional<interfaces::BlockRef> getTip() override
    {
        if (m_tip.IsNull()) return std::nullopt;
        return interfaces::BlockRef{m_tip, /*height=*/0};
    }

    interfaces::BlockRef waitTipChanged(interfaces::uint256 current_tip,
                                        interfaces::MillisecondsDouble timeout) override
    {
        std::unique_lock<std::mutex> lock(m_mu);
        m_last_current_tip = current_tip;
        const uint64_t epoch = m_interrupt_epoch;
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        const bool interrupted = m_cv.wait_until(lock, deadline,
            [&] { return m_interrupt_epoch != epoch; });
        if (interrupted) {
            // Signal "no result" by returning a null BlockRef — matches
            // v31.0's wire-level behavior on shutdown/interrupt.
            return interfaces::BlockRef{};
        }
        return interfaces::BlockRef{m_tip, /*height=*/0};
    }

    void interrupt() override
    {
        std::lock_guard<std::mutex> lock(m_mu);
        ++m_interrupt_epoch;
        m_cv.notify_all();
    }

    void set_tip(interfaces::uint256 tip)
    {
        std::lock_guard<std::mutex> lock(m_mu);
        m_tip = tip;
    }

    interfaces::uint256 last_current_tip()
    {
        std::lock_guard<std::mutex> lock(m_mu);
        return m_last_current_tip;
    }

private:
    std::mutex m_mu;
    std::condition_variable m_cv;
    interfaces::uint256 m_tip;
    interfaces::uint256 m_last_current_tip;
    uint64_t m_interrupt_epoch{0};
};

} // namespace mock_ipc

#endif // BITCOIN_TUI_TESTS_MOCK_IPC_MINING_V31_H

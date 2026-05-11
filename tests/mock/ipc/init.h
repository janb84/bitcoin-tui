// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_TESTS_MOCK_IPC_INIT_H
#define BITCOIN_TUI_TESTS_MOCK_IPC_INIT_H

#include "interfaces/init.h"
#include "interfaces/mining.h"
#include "interfaces/types.h"

#include <memory>
#include <optional>
#include <utility>

namespace mock_ipc {

// Init mock that hands out a wrapper around a shared Mining instance, so
// the underlying mock survives multiple connect/disconnect cycles within
// a single test. Used for tests that need a v31.0-flavoured server.
class Init : public interfaces::Init
{
public:
    explicit Init(std::shared_ptr<interfaces::Mining> m) : m_mining(std::move(m)) {}
    std::unique_ptr<interfaces::Mining> makeMining() override
    {
        struct Wrapper : interfaces::Mining {
            interfaces::Mining* inner;
            explicit Wrapper(interfaces::Mining* m) : inner(m) {}
            bool isTestChain() override            { return inner->isTestChain(); }
            bool isInitialBlockDownload() override { return inner->isInitialBlockDownload(); }
            std::optional<interfaces::BlockRef> getTip() override { return inner->getTip(); }
            interfaces::BlockRef waitTipChanged(interfaces::uint256 t, interfaces::MillisecondsDouble to) override { return inner->waitTipChanged(t, to); }
            void interrupt() override              { inner->interrupt(); }
        };
        return std::make_unique<Wrapper>(m_mining.get());
    }
private:
    std::shared_ptr<interfaces::Mining> m_mining;
};

} // namespace mock_ipc

#endif // BITCOIN_TUI_TESTS_MOCK_IPC_INIT_H

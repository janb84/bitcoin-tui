// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_TESTS_MOCK_IPC_INIT_V30_H
#define BITCOIN_TUI_TESTS_MOCK_IPC_INIT_V30_H

#include "interfaces/init.h"
#include "interfaces/mining.h"

#include <memory>
#include <stdexcept>

namespace mock_ipc {

// Init mock that simulates a v30.x server: makeMining throws (matching
// libmultiprocess's "Method not implemented; methodId = 3" wire-level
// fault on a real v30.x bitcoin-node, where Init.makeMining lives at
// ordinal 2 and ordinal 3 is unallocated).
class InitV30 : public interfaces::Init
{
public:
    std::unique_ptr<interfaces::Mining> makeMining() override
    {
        throw std::runtime_error("Method not implemented; methodId = 3");
    }
};

} // namespace mock_ipc

#endif // BITCOIN_TUI_TESTS_MOCK_IPC_INIT_V30_H

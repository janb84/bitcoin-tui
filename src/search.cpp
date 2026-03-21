#include <algorithm>

#include "format.hpp"
#include "search.hpp"

// ============================================================================
// Transaction / block lookup — pure: takes config + query, returns result.
// No shared state, no threads, no UI side-effects. Suitable for testing.
// ============================================================================
TxSearchState perform_tx_search(const RpcConfig& cfg, const RpcAuth& auth, const std::string& query,
                                bool query_is_height, int64_t tip,
                                const std::string& blockhash_hint) {
    TxSearchState result;
    result.txid = query;
    try {
        RpcConfig search_cfg       = cfg;
        search_cfg.timeout_seconds = 5;
        RpcClient search_rpc(search_cfg, auth);

        // Helper: populate result with block metadata from getblock (verbosity 1).
        auto fetch_block = [&](const std::string& hash) {
            auto blk                 = search_rpc.call("getblock", {json(hash), json(1)})["result"];
            result.blk_hash          = blk.value("hash", hash);
            result.blk_height        = blk.value("height", 0LL);
            result.blk_time          = blk.value("time", 0LL);
            result.blk_ntx           = blk.value("nTx", 0LL);
            result.blk_size          = blk.value("size", 0LL);
            result.blk_weight        = blk.value("weight", 0LL);
            result.blk_difficulty    = blk.value("difficulty", 0.0);
            result.blk_confirmations = blk.value("confirmations", 0LL);
            // Extract miner tag from coinbase scriptSig
            if (blk.contains("tx") && blk["tx"].is_array() && !blk["tx"].empty()) {
                std::string coinbase_txid = blk["tx"][0].get<std::string>();
                try {
                    auto coinbase_tx =
                        search_rpc.call("getrawtransaction",
                                        {json(coinbase_txid), json(true), json(hash)})["result"];
                    if (coinbase_tx.contains("vin") && coinbase_tx["vin"].is_array() &&
                        !coinbase_tx["vin"].empty()) {
                        std::string cb_hex = coinbase_tx["vin"][0].value("coinbase", "");
                        result.blk_miner   = extract_miner(cb_hex);
                    }
                } catch (...) {
                    result.blk_miner = "—";
                }
            }
            result.is_block = true;
            result.found    = true;
        };

        if (query_is_height) {
            // Block height search: getblockhash → getblock
            int64_t     height = std::stoll(query);
            auto        hash_r = search_rpc.call("getblockhash", {height})["result"];
            std::string hash   = hash_r.get<std::string>();
            fetch_block(hash);
        } else {
            // 1. Try mempool first
            try {
                auto entry = search_rpc.call("getmempoolentry", {query})["result"];

                if (entry.contains("fees") && entry["fees"].is_object())
                    result.fee = entry["fees"].value("base", 0.0);
                else
                    result.fee = entry.value("fee", 0.0);

                result.vsize       = entry.value("vsize", 0LL);
                result.weight      = entry.value("weight", 0LL);
                result.ancestors   = entry.value("ancestorcount", 0LL);
                result.descendants = entry.value("descendantcount", 0LL);
                result.entry_time  = entry.value("time", 0LL);
                if (result.vsize > 0)
                    result.fee_rate = result.fee * 1e8 / static_cast<double>(result.vsize);
                result.confirmed = false;
                result.found     = true;
            } catch (...) {
                // 2. Try confirmed tx (txindex=1, or blockhash hint)
                try {
                    json tx_params = blockhash_hint.empty()
                                         ? json{json(query), json(true)}
                                         : json{json(query), json(true), json(blockhash_hint)};
                    auto tx        = search_rpc.call("getrawtransaction", tx_params)["result"];

                    result.vsize         = tx.value("vsize", 0LL);
                    result.weight        = tx.value("weight", 0LL);
                    result.blockhash     = tx.value("blockhash", "");
                    result.confirmations = tx.value("confirmations", 0LL);
                    result.blocktime     = tx.value("blocktime", 0LL);

                    if (tip > 0 && result.confirmations > 0)
                        result.block_height = tip - result.confirmations + 1;

                    if (tx.contains("vin") && tx["vin"].is_array()) {
                        for (const auto& inp : tx["vin"]) {
                            TxVin v;
                            if (inp.contains("coinbase")) {
                                v.is_coinbase = true;
                            } else {
                                v.txid = inp.value("txid", "");
                                v.vout = inp.value("vout", 0);
                            }
                            result.vin_list.push_back(v);
                        }
                        result.vin_count = static_cast<int>(result.vin_list.size());
                    }
                    if (tx.contains("vout") && tx["vout"].is_array()) {
                        for (const auto& out : tx["vout"]) {
                            TxVout v;
                            v.value = out.value("value", 0.0);
                            if (out.contains("scriptPubKey")) {
                                const auto& spk = out["scriptPubKey"];
                                v.type          = spk.value("type", "");
                                if (spk.contains("address"))
                                    v.address = spk.value("address", "");
                            }
                            result.total_output += v.value;
                            result.vout_list.push_back(v);
                        }
                        result.vout_count = static_cast<int>(result.vout_list.size());
                    }
                    result.confirmed = true;
                    result.found     = true;
                } catch (...) {
                    // 3. Fall back: try as block hash
                    fetch_block(query);
                }
            }
        }
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

TxSearchState perform_block_search(const RpcConfig& cfg, const RpcAuth& auth, int64_t height) {
    TxSearchState result;
    result.txid = std::to_string(height);
    try {
        RpcConfig search_cfg       = cfg;
        search_cfg.timeout_seconds = 30; // verbosity-3 getblock can be 30-50MB
        RpcClient search_rpc(search_cfg, auth);

        auto        hash_r = search_rpc.call("getblockhash", {height})["result"];
        std::string hash   = hash_r.get<std::string>();

        // Verbosity 3: full tx objects with prevout values for fee calculation.
        auto blk                 = search_rpc.call("getblock", {json(hash), json(3)})["result"];
        result.blk_hash          = blk.value("hash", hash);
        result.blk_height        = blk.value("height", 0LL);
        result.blk_time          = blk.value("time", 0LL);
        result.blk_ntx           = blk.value("nTx", 0LL);
        result.blk_size          = blk.value("size", 0LL);
        result.blk_weight        = blk.value("weight", 0LL);
        result.blk_difficulty    = blk.value("difficulty", 0.0);
        result.blk_confirmations = blk.value("confirmations", 0LL);
        if (blk.contains("tx") && blk["tx"].is_array()) {
            bool first = true;
            for (const auto& tx : blk["tx"]) {
                if (!tx.is_object())
                    continue;
                TxListEntry entry;
                entry.txid       = tx.value("txid", "");
                entry.vsize      = tx.value("vsize", 0LL);
                bool is_coinbase = tx.contains("vin") && tx["vin"].is_array() &&
                                   !tx["vin"].empty() && tx["vin"][0].contains("coinbase");
                if (first) {
                    if (is_coinbase)
                        result.blk_miner = extract_miner(tx["vin"][0].value("coinbase", ""));
                    first = false;
                }
                if (!is_coinbase && tx.contains("vin") && tx.contains("vout")) {
                    double in_val = 0.0, out_val = 0.0;
                    for (const auto& inp : tx["vin"])
                        if (inp.contains("prevout"))
                            in_val += inp["prevout"].value("value", 0.0);
                    for (const auto& out : tx["vout"])
                        out_val += out.value("value", 0.0);
                    double fee_btc = in_val - out_val;
                    if (fee_btc > 0.0 && entry.vsize > 0)
                        entry.feerate = fee_btc * 1e8 / static_cast<double>(entry.vsize);
                }
                result.blk_tx_list.push_back(std::move(entry));
            }
            if (result.blk_miner.empty())
                result.blk_miner = "—";
        }
        result.is_block = true;
        result.found    = true;
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

TxSearchState perform_mempool_search(const RpcConfig& cfg, const RpcAuth& auth) {
    TxSearchState result;
    result.txid = "mempool";
    try {
        RpcConfig search_cfg       = cfg;
        search_cfg.timeout_seconds = 60; // large mempool JSON can be 50MB+
        RpcClient search_rpc(search_cfg, auth);
        // verbose=true returns an object keyed by txid with fee/size data.
        auto txids        = search_rpc.call("getrawmempool", {true})["result"];
        result.is_mempool = true;
        result.found      = true;
        if (txids.is_object()) {
            for (const auto& [txid, info] : txids.items()) {
                TxListEntry entry;
                entry.txid  = txid;
                entry.vsize = info.value("vsize", 0LL);
                if (info.contains("fees") && info["fees"].is_object()) {
                    const auto& fees        = info["fees"];
                    int64_t     chunkweight = info.value("chunkweight", 0LL);
                    if (fees.contains("chunk") && chunkweight > 0) {
                        // Cluster mempool: chunk feerate reflects mining priority.
                        entry.feerate =
                            fees.value("chunk", 0.0) * 1e8 * 4.0 / static_cast<double>(chunkweight);
                    } else if (entry.vsize > 0) {
                        entry.feerate =
                            fees.value("base", 0.0) * 1e8 / static_cast<double>(entry.vsize);
                    }
                }
                result.blk_tx_list.push_back(std::move(entry));
            }
            // Sort highest feerate first (mining priority order).
            std::sort(
                result.blk_tx_list.begin(), result.blk_tx_list.end(),
                [](const TxListEntry& a, const TxListEntry& b) { return a.feerate > b.feerate; });
        }
    } catch (const std::exception& e) {
        result.error      = e.what();
        result.is_mempool = true; // keep flag so error is visible in the UI
    }
    return result;
}

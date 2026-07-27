#pragma once

#include <fstream>
#include <istream>
#include <map>
#include <sstream>
#include <string>

// ============================================================================
// Minimal reader for Bitcoin Core's bitcoin.conf
//
// Only the connection settings bitcoin-tui cares about are consumed by the
// caller, but the whole file is parsed so lookups behave the way the node
// itself resolves them:
//
//   key=value            applies to every network
//   [main] [test] [testnet4] [signet] [regtest]
//                        section: applies to that network only
//   regtest.key=value    prefixed form, equivalent to a section entry
//   # or ; to end of line is a comment
//
// A network-specific value beats a global one. Section names follow Core, so
// testnet3 is spelled [test]; [testnet3] is not a thing and is ignored here
// exactly as the node ignores it.
// ============================================================================
class BitcoinConf {
  public:
    // Core's section name for one of our network ids ("" when unknown).
    static std::string section_for(const std::string& network) {
        if (network == "main")
            return "main";
        if (network == "testnet3")
            return "test"; // Core spells testnet3 [test]
        if (network == "testnet4")
            return "testnet4";
        if (network == "signet")
            return "signet";
        if (network == "regtest")
            return "regtest";
        return "";
    }

    static BitcoinConf parse(std::istream& in) {
        BitcoinConf conf;
        std::string line;
        std::string section; // current [section], empty at top level
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            // Strip comments. Core has no escaping here, so this is faithful.
            if (auto hash = line.find_first_of("#;"); hash != std::string::npos)
                line.erase(hash);
            line = trim(line);
            if (line.empty())
                continue;

            if (line.front() == '[' && line.back() == ']') {
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            if (key.empty())
                continue;

            // A `net.key` prefix is equivalent to putting key in [net].
            std::string scope = section;
            if (auto dot = key.find('.'); dot != std::string::npos) {
                std::string prefix = key.substr(0, dot);
                if (is_section(prefix)) {
                    scope = prefix;
                    key   = key.substr(dot + 1);
                }
            }
            if (key.empty())
                continue;
            if (scope.empty())
                conf.global_[key] = val;
            else
                conf.per_net_[scope][key] = val;
        }
        return conf;
    }

    // Missing or unreadable file yields an empty config; absence is normal.
    static BitcoinConf load(const std::string& path) {
        std::ifstream f(path);
        if (!f)
            return {};
        return parse(f);
    }

    // Value of `key` for `network`, preferring the network-specific entry.
    // Returns "" when unset.
    std::string get(const std::string& key, const std::string& network) const {
        if (const std::string sec = section_for(network); !sec.empty()) {
            if (auto n = per_net_.find(sec); n != per_net_.end()) {
                if (auto it = n->second.find(key); it != n->second.end())
                    return it->second;
            }
        }
        if (auto it = global_.find(key); it != global_.end())
            return it->second;
        return "";
    }

    bool empty() const { return global_.empty() && per_net_.empty(); }

  private:
    static bool is_section(const std::string& s) {
        return s == "main" || s == "test" || s == "testnet4" || s == "signet" || s == "regtest";
    }

    static std::string trim(const std::string& s) {
        auto b = s.find_first_not_of(" \t");
        if (b == std::string::npos)
            return "";
        auto e = s.find_last_not_of(" \t");
        return s.substr(b, e - b + 1);
    }

    std::map<std::string, std::string>                        global_;
    std::map<std::string, std::map<std::string, std::string>> per_net_;
};

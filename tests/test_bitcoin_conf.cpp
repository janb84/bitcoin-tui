#include <sstream>

#include <catch2/catch_test_macros.hpp>

#include "bitcoin_conf.hpp"

static BitcoinConf from(const std::string& text) {
    std::istringstream in(text);
    return BitcoinConf::parse(in);
}

TEST_CASE("bitcoin.conf — empty and missing", "[bitcoin_conf]") {
    CHECK(from("").empty());
    CHECK(BitcoinConf::load("/nonexistent/path/bitcoin.conf").empty());
    CHECK(from("# just a comment\n\n").empty());
}

TEST_CASE("bitcoin.conf — global key/value", "[bitcoin_conf]") {
    auto c = from("rpcport=9999\nrpcuser=alice\n");
    CHECK(c.get("rpcport", "main") == "9999");
    CHECK(c.get("rpcuser", "regtest") == "alice"); // global applies to every network
    CHECK(c.get("rpcpassword", "main").empty());
}

TEST_CASE("bitcoin.conf — whitespace and comments", "[bitcoin_conf]") {
    auto c = from("  rpcport =  8888   # inline comment\n"
                  "; full line comment\n"
                  "rpcconnect\t=\t10.0.0.5\n");
    CHECK(c.get("rpcport", "main") == "8888");
    CHECK(c.get("rpcconnect", "main") == "10.0.0.5");
}

TEST_CASE("bitcoin.conf — network section overrides global", "[bitcoin_conf]") {
    auto c = from("rpcport=8332\n"
                  "[regtest]\n"
                  "rpcport=19999\n");
    CHECK(c.get("rpcport", "regtest") == "19999");
    CHECK(c.get("rpcport", "main") == "8332");
    CHECK(c.get("rpcport", "signet") == "8332");
}

TEST_CASE("bitcoin.conf — prefixed keys equal section entries", "[bitcoin_conf]") {
    auto c = from("regtest.rpcport=19999\nmain.rpcuser=bob\n");
    CHECK(c.get("rpcport", "regtest") == "19999");
    CHECK(c.get("rpcport", "main").empty());
    CHECK(c.get("rpcuser", "main") == "bob");
    CHECK(c.get("rpcuser", "regtest").empty());
}

TEST_CASE("bitcoin.conf — testnet3 is spelled [test]", "[bitcoin_conf]") {
    CHECK(BitcoinConf::section_for("testnet3") == "test");
    auto c = from("[test]\nrpcport=18332\n");
    CHECK(c.get("rpcport", "testnet3") == "18332");
    CHECK(c.get("rpcport", "main").empty());
}

TEST_CASE("bitcoin.conf — testnet4 and signet have their own sections", "[bitcoin_conf]") {
    auto c = from("[testnet4]\nrpcport=48332\n[signet]\nrpcport=38332\n");
    CHECK(c.get("rpcport", "testnet4") == "48332");
    CHECK(c.get("rpcport", "signet") == "38332");
    CHECK(c.get("rpcport", "testnet3").empty());
}

TEST_CASE("bitcoin.conf — [testnet3] is not a Core section", "[bitcoin_conf]") {
    // Core does not recognise [testnet3]; entries under it must not leak into
    // the testnet3 lookup, or bitcoin-tui would disagree with the node.
    auto c = from("[testnet3]\nrpcport=18332\n");
    CHECK(c.get("rpcport", "testnet3").empty());
}

TEST_CASE("bitcoin.conf — later duplicate wins", "[bitcoin_conf]") {
    auto c = from("rpcport=1\nrpcport=2\n");
    CHECK(c.get("rpcport", "main") == "2");
}

TEST_CASE("bitcoin.conf — malformed lines are skipped", "[bitcoin_conf]") {
    auto c = from("this line has no equals sign\n"
                  "=novalue\n"
                  "rpcport=7777\n");
    CHECK(c.get("rpcport", "main") == "7777");
}

TEST_CASE("bitcoin.conf — value may contain '=' and spaces", "[bitcoin_conf]") {
    auto c = from("rpcpassword=a=b=c\n");
    CHECK(c.get("rpcpassword", "main") == "a=b=c");
}

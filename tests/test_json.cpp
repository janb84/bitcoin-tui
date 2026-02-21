#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "json.hpp"

// ============================================================================
// Construction + type queries
// ============================================================================

TEST_CASE("null") {
    json j;
    CHECK(j.is_null());
    CHECK(!j.is_bool());
    CHECK(!j.is_number());
    CHECK(!j.is_string());
    CHECK(!j.is_array());
    CHECK(!j.is_object());
    CHECK(j.size() == 0);
    CHECK(j.empty());
}

TEST_CASE("nullptr_t construction") {
    json j(nullptr);
    CHECK(j.is_null());
}

TEST_CASE("bool") {
    json t(true);
    CHECK(t.is_bool());
    CHECK(t.get<bool>() == true);

    json f(false);
    CHECK(f.is_bool());
    CHECK(f.get<bool>() == false);
}

TEST_CASE("integer") {
    SECTION("int") {
        json j(42);
        CHECK(j.is_number());
        CHECK(j.is_number_integer());
        CHECK(!j.is_number_float());
        CHECK(j.get<int>() == 42);
        CHECK(j.get<int64_t>() == 42);
    }
    SECTION("negative") {
        json j(-7);
        CHECK(j.get<int>() == -7);
    }
    SECTION("long long") {
        json j(884231LL);
        CHECK(j.get<int64_t>() == 884231LL);
    }
    SECTION("size_t") {
        json j(size_t{99});
        CHECK(j.get<int>() == 99);
    }
    SECTION("zero") {
        json j(0);
        CHECK(j.get<int>() == 0);
    }
}

TEST_CASE("float") {
    SECTION("double") {
        json j(3.14);
        CHECK(j.is_number());
        CHECK(j.is_number_float());
        CHECK(!j.is_number_integer());
        CHECK(j.get<double>() == Catch::Approx(3.14));
    }
    SECTION("float") {
        json j(1.5f);
        CHECK(j.get<float>() == Catch::Approx(1.5f));
    }
    SECTION("negative") {
        json j(-0.001);
        CHECK(j.get<double>() == Catch::Approx(-0.001));
    }
}

TEST_CASE("string") {
    SECTION("from const char*") {
        json j("hello");
        CHECK(j.is_string());
        CHECK(j.get<std::string>() == "hello");
    }
    SECTION("from std::string") {
        std::string s = "world";
        json j(s);
        CHECK(j.get<std::string>() == "world");
    }
    SECTION("from rvalue string") {
        json j(std::string("moved"));
        CHECK(j.get<std::string>() == "moved");
    }
    SECTION("empty string") {
        json j("");
        CHECK(j.is_string());
        CHECK(j.get<std::string>().empty());
    }
}

TEST_CASE("array factory") {
    json a = json::array();
    CHECK(a.is_array());
    CHECK(a.empty());
    CHECK(a.size() == 0);
}

TEST_CASE("object factory") {
    json o = json::object();
    CHECK(o.is_object());
    CHECK(o.empty());
}

// ============================================================================
// Initializer-list construction
// ============================================================================

TEST_CASE("initializer list — array") {
    json a = {1, 2, 3};
    CHECK(a.is_array());
    CHECK(a.size() == 3);
    CHECK(a[0].get<int>() == 1);
    CHECK(a[1].get<int>() == 2);
    CHECK(a[2].get<int>() == 3);
}

TEST_CASE("initializer list — object") {
    json o = {{"key", "value"}, {"num", 42}};
    CHECK(o.is_object());
    CHECK(o.size() == 2);
    CHECK(o["key"].get<std::string>() == "value");
    CHECK(o["num"].get<int>() == 42);
}

TEST_CASE("initializer list — empty braces yield null") {
    // {} invokes the default constructor, not the initializer_list overload
    json e = {};
    CHECK(e.is_null());
}

// ============================================================================
// Operator[]
// ============================================================================

TEST_CASE("object operator[] write + read") {
    json o;
    o["x"] = 10;
    o["y"] = "hi";
    CHECK(o.is_object());
    CHECK(o["x"].get<int>() == 10);
    CHECK(o["y"].get<std::string>() == "hi");
}

TEST_CASE("const object operator[] missing key returns null") {
    const json o = {{"a", 1}};
    CHECK(o["a"].get<int>() == 1);
    CHECK(o["missing"].is_null());
}

TEST_CASE("array operator[]") {
    json a = {10, 20, 30};
    CHECK(a[0].get<int>() == 10);
    CHECK(a[2].get<int>() == 30);
    a[1] = 99;
    CHECK(a[1].get<int>() == 99);
}

TEST_CASE("operator[] on non-object throws") {
    json j(42);
    CHECK_THROWS_AS(j["key"], json::exception);
}

TEST_CASE("array operator[] on non-array throws") {
    const json j("str");
    CHECK_THROWS_AS(j[0], json::exception);
}

// ============================================================================
// contains / value
// ============================================================================

TEST_CASE("contains") {
    json o = {{"x", 1}, {"y", nullptr}};
    CHECK(o.contains("x"));
    CHECK(o.contains("y"));
    CHECK(!o.contains("z"));

    json a = json::array();
    CHECK(!a.contains("x"));

    json n;
    CHECK(!n.contains("x"));
}

TEST_CASE("value with default — key present") {
    json o = {{"n", 7}, {"f", 3.14}, {"s", "hi"}, {"b", true}};
    CHECK(o.value("n", 0)    == 7);
    CHECK(o.value("f", 0.0)  == Catch::Approx(3.14));
    CHECK(o.value("b", false) == true);
    CHECK(o.value("s", "")   == "hi");
}

TEST_CASE("value with default — key missing") {
    json o = {{"x", 1}};
    CHECK(o.value("missing", 99)     == 99);
    CHECK(o.value("missing", 0.5)    == Catch::Approx(0.5));
    CHECK(o.value("missing", false)  == false);
    CHECK(o.value("missing", "def")  == "def");
}

TEST_CASE("value with default — null value") {
    json o = {{"k", nullptr}};
    CHECK(o.value("k", 42) == 42);
    CHECK(o.value("k", "fb") == "fb");
}

TEST_CASE("value on non-object returns default") {
    json a = json::array();
    CHECK(a.value("x", 0) == 0);
}

// ============================================================================
// get<T> error cases
// ============================================================================

TEST_CASE("get type mismatches throw") {
    json n(42);
    CHECK_THROWS_AS(n.get<bool>(),        json::exception);
    CHECK_THROWS_AS(n.get<std::string>(), json::exception);

    json b(true);
    CHECK_THROWS_AS(b.get<int>(),         json::exception);

    json s("hi");
    CHECK_THROWS_AS(s.get<double>(),      json::exception);
}

// ============================================================================
// Iteration
// ============================================================================

TEST_CASE("range-for over array") {
    json a = {1, 2, 3};
    int sum = 0;
    for (const auto& el : a) sum += el.get<int>();
    CHECK(sum == 6);
}

TEST_CASE("range-for over empty array") {
    json a = json::array();
    int count = 0;
    for (const auto& _ : a) ++count;
    CHECK(count == 0);
}

// ============================================================================
// parse — primitives
// ============================================================================

TEST_CASE("parse null") {
    auto j = json::parse("null");
    CHECK(j.is_null());
}

TEST_CASE("parse bool") {
    CHECK(json::parse("true").get<bool>()  == true);
    CHECK(json::parse("false").get<bool>() == false);
}

TEST_CASE("parse integer") {
    CHECK(json::parse("0").get<int>()     == 0);
    CHECK(json::parse("42").get<int>()    == 42);
    CHECK(json::parse("-7").get<int>()    == -7);
    CHECK(json::parse("884231").get<int64_t>() == 884231LL);
}

TEST_CASE("parse float") {
    CHECK(json::parse("3.14").get<double>()  == Catch::Approx(3.14));
    CHECK(json::parse("-0.5").get<double>()  == Catch::Approx(-0.5));
    CHECK(json::parse("1e3").get<double>()   == Catch::Approx(1000.0));
    CHECK(json::parse("1.5e2").get<double>() == Catch::Approx(150.0));
    CHECK(json::parse("2E-1").get<double>()  == Catch::Approx(0.2));
}

TEST_CASE("parse string") {
    CHECK(json::parse(R"("hello")").get<std::string>() == "hello");
    CHECK(json::parse(R"("")").get<std::string>().empty());
}

TEST_CASE("parse string escapes") {
    CHECK(json::parse(R"("a\"b")").get<std::string>()  == "a\"b");
    CHECK(json::parse(R"("a\\b")").get<std::string>()  == "a\\b");
    CHECK(json::parse(R"("a\/b")").get<std::string>()  == "a/b");
    CHECK(json::parse(R"("a\nb")").get<std::string>()  == "a\nb");
    CHECK(json::parse(R"("a\tb")").get<std::string>()  == "a\tb");
    CHECK(json::parse(R"("a\rb")").get<std::string>()  == "a\rb");
}

TEST_CASE("parse unicode escape") {
    // \u0041 == 'A' (ASCII)
    CHECK(json::parse(R"("\u0041")").get<std::string>() == "A");
    // \u00e9 == é (UTF-8: 0xC3 0xA9)
    auto s = json::parse(R"("\u00e9")").get<std::string>();
    CHECK(s.size() == 2);
    CHECK(static_cast<unsigned char>(s[0]) == 0xC3);
    CHECK(static_cast<unsigned char>(s[1]) == 0xA9);
}

// ============================================================================
// parse — arrays and objects
// ============================================================================

TEST_CASE("parse empty array") {
    auto j = json::parse("[]");
    CHECK(j.is_array());
    CHECK(j.empty());
}

TEST_CASE("parse array") {
    auto j = json::parse("[1, 2, 3]");
    CHECK(j.is_array());
    CHECK(j.size() == 3);
    CHECK(j[0].get<int>() == 1);
    CHECK(j[1].get<int>() == 2);
    CHECK(j[2].get<int>() == 3);
}

TEST_CASE("parse array with mixed types") {
    auto j = json::parse(R"([null, true, 1, 1.5, "x"])");
    CHECK(j[0].is_null());
    CHECK(j[1].get<bool>()        == true);
    CHECK(j[2].get<int>()         == 1);
    CHECK(j[3].get<double>()      == Catch::Approx(1.5));
    CHECK(j[4].get<std::string>() == "x");
}

TEST_CASE("parse empty object") {
    auto j = json::parse("{}");
    CHECK(j.is_object());
    CHECK(j.empty());
}

TEST_CASE("parse object") {
    auto j = json::parse(R"({"a":1,"b":"two"})");
    CHECK(j.is_object());
    CHECK(j.size() == 2);
    CHECK(j["a"].get<int>()         == 1);
    CHECK(j["b"].get<std::string>() == "two");
}

TEST_CASE("parse nested") {
    auto j = json::parse(R"({"result":{"blocks":884231,"chain":"main"}})");
    CHECK(j["result"]["blocks"].get<int64_t>() == 884231LL);
    CHECK(j["result"]["chain"].get<std::string>() == "main");
}

TEST_CASE("parse whitespace is ignored") {
    auto j = json::parse("  {  \"k\"  :  42  }  ");
    CHECK(j["k"].get<int>() == 42);
}

// ============================================================================
// parse — error cases
// ============================================================================

TEST_CASE("parse errors throw json::exception") {
    CHECK_THROWS_AS(json::parse(""),           json::exception);
    CHECK_THROWS_AS(json::parse("{"),          json::exception);
    CHECK_THROWS_AS(json::parse("["),          json::exception);
    CHECK_THROWS_AS(json::parse("tru"),        json::exception);
    CHECK_THROWS_AS(json::parse("nul"),        json::exception);
    CHECK_THROWS_AS(json::parse(R"("unterminated)"), json::exception);
    CHECK_THROWS_AS(json::parse("42 extra"),   json::exception);
    CHECK_THROWS_AS(json::parse("{\"k\":}"),   json::exception);
}

// ============================================================================
// dump
// ============================================================================

TEST_CASE("dump primitives") {
    CHECK(json().dump()        == "null");
    CHECK(json(true).dump()   == "true");
    CHECK(json(false).dump()  == "false");
    CHECK(json(42).dump()     == "42");
    CHECK(json(-7).dump()     == "-7");
    CHECK(json("hi").dump()   == "\"hi\"");
}

TEST_CASE("dump string escaping") {
    CHECK(json("a\"b").dump()  == R"("a\"b")");
    CHECK(json("a\\b").dump()  == R"("a\\b")");
    CHECK(json("a\nb").dump()  == R"("a\nb")");
    CHECK(json("a\tb").dump()  == R"("a\tb")");
}

TEST_CASE("dump array") {
    json a = {1, 2, 3};
    CHECK(a.dump() == "[1,2,3]");
}

TEST_CASE("dump empty array") {
    CHECK(json::array().dump() == "[]");
}

TEST_CASE("dump empty object") {
    CHECK(json::object().dump() == "{}");
}

TEST_CASE("dump parse round-trip") {
    const std::string src = R"({"a":1,"b":"two","c":[true,null]})";
    CHECK(json::parse(src).dump() == src);
}

TEST_CASE("dump pretty indents") {
    json o = {{"x", 1}};
    const std::string pretty = o.dump(2);
    CHECK(pretty.find('\n') != std::string::npos);
    CHECK(pretty.find("  ") != std::string::npos);
}

// ============================================================================
// Bitcoin Core RPC response shape (integration-style)
// ============================================================================

TEST_CASE("getblockchaininfo response shape") {
    const std::string raw = R"({
        "result": {
            "chain": "main",
            "blocks": 884231,
            "headers": 884231,
            "difficulty": 113762235938718.02,
            "verificationprogress": 0.9999978,
            "pruned": false,
            "initialblockdownload": false
        },
        "error": null,
        "id": 1
    })";

    auto j = json::parse(raw);
    CHECK(j["error"].is_null());

    auto r = j["result"];
    CHECK(r.value("chain",   "") == "main");
    CHECK(r.value("blocks",  0LL) == 884231LL);
    CHECK(r.value("headers", 0LL) == 884231LL);
    CHECK(r.value("pruned",  true)  == false);
    CHECK(r.value("initialblockdownload", true) == false);
    CHECK(r.value("verificationprogress", 0.0) == Catch::Approx(0.9999978).epsilon(1e-6));
}

TEST_CASE("getpeerinfo response shape") {
    const std::string raw = R"({
        "result": [
            {
                "id": 0,
                "addr": "144.76.31.85:8333",
                "network": "ipv4",
                "subver": "/Satoshi:27.0.0/",
                "inbound": false,
                "bytessent": 12345678,
                "bytesrecv": 98765432,
                "pingtime": 0.014,
                "synced_blocks": 884231
            }
        ],
        "error": null,
        "id": 2
    })";

    auto j   = json::parse(raw);
    auto res = j["result"];
    CHECK(res.is_array());
    CHECK(res.size() == 1);

    auto p = res[0];
    CHECK(p.value("id",      -1)  == 0);
    CHECK(p.value("addr",    "")  == "144.76.31.85:8333");
    CHECK(p.value("network", "")  == "ipv4");
    CHECK(p.value("inbound", true) == false);
    CHECK(p.value("bytessent",     0LL) == 12345678LL);
    CHECK(p.value("bytesrecv",     0LL) == 98765432LL);
    CHECK(p.value("synced_blocks", 0LL) == 884231LL);

    CHECK(p.contains("pingtime"));
    CHECK(p["pingtime"].is_number());
    CHECK(p["pingtime"].get<double>() * 1000.0 == Catch::Approx(14.0).epsilon(1e-6));
}

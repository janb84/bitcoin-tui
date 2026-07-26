-- Script-level test for lua/tabs/mempool.lua.
--
-- Stubs the btcui_* API (no TUI, no RPC, no threads) and drives the tab through
-- its flows: stats/blocks refresh, the global search (mempool tx, confirmed tx,
-- block hash, height, error), the Inputs/Outputs sub-overlays, drill-downs and
-- the Esc history stack.
--
-- Run:  lua tests/lua/test_mempool_tab.lua [path/to/mempool.lua]
-- Wired into CTest as `test_mempool_tab` (see tests/CMakeLists.txt). Exits
-- non-zero on the first failed check.

local script = (arg and arg[1]) or "lua/tabs/mempool.lua"

----------------------------------------------------------------------
-- btcui_* stubs
----------------------------------------------------------------------

local rpc_calls   = {}  -- every btcui_rpc invocation, in order
local rpc_results = {}  -- canned result per method ("ERROR" raises)
local last_dialog = nil -- opts of the most recent btcui_dialog call
local on_select_fn                     -- captured btcui_on_select callback
local on_search_fn                     -- captured btcui_on_search callback
local timer_fn                         -- captured btcui_set_interval callback
local summaries = {}                   -- created btcui_summary objects
local blocks_panels = {}               -- created btcui_blocks objects
local tab_name
local quit_called = false

btcui_set_name     = function(n) tab_name = n end
btcui_option       = function(_, d) return d end
btcui_summary      = function(opts)
    local s = { opts = opts, values = {} }
    function s:set(v) for k, val in pairs(v) do self.values[k] = val end end
    summaries[#summaries + 1] = s
    return s
end
btcui_blocks       = function(opts)
    local b = { opts = opts, bars = {} }
    function b:set(bars) self.bars = bars end
    blocks_panels[#blocks_panels + 1] = b
    return b
end
btcui_gauge        = function(frac, opts) return { gauge = frac, opts = opts } end
btcui_key_hint     = function() end
btcui_on_select    = function(fn) on_select_fn = fn end
btcui_on_search    = function(fn) on_search_fn = fn end
btcui_set_interval = function(_, fn) timer_fn = fn; return {} end
btcui_wake         = function() end
btcui_now          = function() return 1000000 end
btcui_localtime    = function(ts) return "LT:" .. tostring(ts) end
btcui_quit         = function() quit_called = true end
btcui_dialog       = function(opts) last_dialog = opts end
btcui_dialog_close = function() last_dialog = nil end
btcui_rpc          = function(method, ...)
    rpc_calls[#rpc_calls + 1] = { method = method, ... }
    local r = rpc_results[method]
    if r == "ERROR" then error("rpc failed: " .. method) end
    return r
end

----------------------------------------------------------------------
-- Canned RPC data
----------------------------------------------------------------------

rpc_results["getmempoolinfo"] = {
    size = 41234, bytes = 17000000, usage = 250000000, maxmempool = 300000000,
    total_fee = 0.53210000, mempoolminfee = 0.00001,
}
rpc_results["getblockchaininfo"] = { blocks = 2 }
rpc_results["getblockstats"] = {
    height = 2, txs = 3412, total_size = 1700000, total_weight = 3800000,
    time = 999820,
}

local TXID = string.rep("ab", 32)

----------------------------------------------------------------------
-- Checks
----------------------------------------------------------------------

local checks = 0
local function check(cond, msg)
    checks = checks + 1
    if not cond then error("FAIL: " .. msg, 2) end
end

local function count_calls(method)
    local n = 0
    for _, c in ipairs(rpc_calls) do
        if c.method == method then n = n + 1 end
    end
    return n
end

local function dialog_has_text(needle)
    for _, r in ipairs(last_dialog.rows) do
        local strs = {}
        if type(r) == "table" then
            strs[#strs + 1] = r.text
            strs[#strs + 1] = r.value
            for _, sp in ipairs(r.spans or {}) do
                strs[#strs + 1] = sp.text
                strs[#strs + 1] = sp.address
            end
        else
            strs[1] = r
        end
        for _, s in ipairs(strs) do
            if type(s) == "string" and s:find(needle, 1, true) then return true end
        end
    end
    return false
end

local function dialog_item(key)
    for _, r in ipairs(last_dialog.rows) do
        if type(r) == "table" and r.key == key then return r end
    end
    return nil
end

dofile(script)

check(tab_name == "Mempool", "tab name set")
check(timer_fn ~= nil, "refresh timer registered")
check(on_search_fn ~= nil, "search handler registered")
check(#summaries == 1 and #blocks_panels == 1, "stats summary + blocks panel created")

-- 1. Refresh populates the stats summary and the blocks panel
timer_fn()
local stats  = summaries[1]
local blocks = blocks_panels[1]
check(stats.values.transactions == "41,234", "tx count grouped")
check(stats.values.vsize == "17.0 MB", "virtual size")
check(stats.values.total_fees == "0.53210000 BTC", "total fees")
check(stats.values.min_fee == "1.0 sat/vB", "mempool min fee in sat/vB")
check(type(stats.values.memory) == "table" and stats.values.memory.gauge > 0.8,
      "memory usage gauge")
check(count_calls("getblockstats") == 3, "block stats fetched for heights 2..0")
check(#blocks.bars == 3 and blocks.bars[1].key == "2", "bars newest-first from tip")
check(blocks.bars[1].label == "2" and blocks.bars[1].fill == 0.95, "bar fill from weight")
check(blocks.bars[1].lines[1] == "3,412 tx", "bar tx line")
check(blocks.bars[1].lines[2] == "1.7 MB", "bar size line")
check(blocks.bars[1].lines[3] == "3m ago", "bar age line")

-- 2. Cached stats aren't re-fetched; a new tip fetches only the new height
timer_fn()
check(count_calls("getblockstats") == 3, "stats cached across refreshes")
rpc_results["getblockchaininfo"] = { blocks = 3 }
timer_fn()
check(count_calls("getblockstats") == 4, "only the new tip fetched")
check(#blocks.bars == 4 and blocks.bars[1].key == "3", "new tip leads the bars")

-- 3. Global search: mempool tx → Searching… then the MEMPOOL overlay
rpc_results["getmempoolentry"] = {
    fees = { base = 0.00012345 }, vsize = 141, weight = 561,
    ancestorcount = 2, descendantcount = 1, time = 999700,
}
on_search_fn(TXID)
check(last_dialog.title == "Transaction Search" and dialog_has_text("Searching"),
      "searching overlay")
check(last_dialog.right_label and last_dialog.right_label:find("…", 1, true),
      "query abbreviated top-right")
timer_fn()
check(dialog_has_text("● MEMPOOL"), "mempool result overlay")
check(dialog_has_text("0.00012345 BTC"), "fee row")
check(dialog_has_text("87.6 sat/vB"), "fee rate from fee/vsize")
check(dialog_has_text("141 vB"), "vsize row")
last_dialog.on_event({ type = "close" })

-- 4. Confirmed tx: item rows for block/inputs/outputs
rpc_results["getmempoolentry"] = "ERROR"
rpc_results["getrawtransaction"] = {
    vsize = 250, weight = 1000, blockhash = string.rep("00", 32),
    confirmations = 2, blocktime = 999000,
    vin  = { { txid = string.rep("cd", 32), vout = 1 }, { coinbase = "aa" } },
    vout = {
        { value = 1.5, scriptPubKey = { type = "witness_v0_keyhash",
                                        address = "bc1qexampleaddress000" } },
        { value = 0.25, scriptPubKey = { type = "nulldata" } },
    },
}
on_search_fn(TXID)
timer_fn()
check(dialog_has_text("✔ CONFIRMED"), "confirmed overlay")
check(dialog_item("block") ~= nil, "block item row")
check(dialog_item("block").spans[2].text == "2", "height = tip(3) - confirmations(2) + 1")
check(dialog_item("inputs") ~= nil and dialog_item("inputs").spans[2].text == "2",
      "inputs item row")
check(dialog_item("outputs") ~= nil and dialog_item("outputs").spans[2].text == "2",
      "outputs item row")
check(dialog_has_text("1.75000000 BTC"), "total out sums the outputs")

-- 5. Outputs sub-overlay: value + address span / script type, Esc goes back
last_dialog.on_event({ type = "select", key = "outputs" })
check(last_dialog.title == "Outputs (2)", "outputs overlay")
check(dialog_has_text("1.50000000 BTC"), "output value")
check(dialog_has_text("bc1qexampleaddress000"), "output address span")
check(dialog_has_text("[nulldata]"), "script type for addressless output")
last_dialog.on_event({ type = "close" })
check(dialog_has_text("✔ CONFIRMED"), "Esc returns to the tx overlay")

-- 6. Inputs sub-overlay: Enter on an input looks its tx up, Esc pops back
last_dialog.on_event({ type = "select", key = "inputs" })
check(last_dialog.title == "Inputs (2)", "inputs overlay")
check(dialog_has_text(string.rep("cd", 32) .. ":1"), "full input txid:vout")
check(dialog_has_text("coinbase"), "coinbase input listed")
check(dialog_item("2") == nil, "coinbase input not selectable")

rpc_results["getmempoolentry"] = {
    fees = { base = 0.0001 }, vsize = 100, weight = 400,
    ancestorcount = 0, descendantcount = 0, time = 999900,
}
last_dialog.on_event({ type = "select", key = "1" })
check(dialog_has_text("Searching"), "input lookup starts a search")
timer_fn()
check(dialog_has_text("● MEMPOOL"), "input tx result overlay")
last_dialog.on_event({ type = "close" })
check(last_dialog.title == "Inputs (2)", "Esc pops back to the inputs overlay")
last_dialog.on_event({ type = "close" })
check(dialog_has_text("✔ CONFIRMED"), "Esc pops back to the tx overlay")

-- 7. Block row drill-down → block overlay, Esc pops back to the tx
rpc_results["getmempoolentry"] = "ERROR"
rpc_results["getrawtransaction"] = "ERROR"
rpc_results["getblock"] = {
    hash = string.rep("00", 32), height = 1, time = 999500, nTx = 2000,
    size = 1500000, weight = 3900000, difficulty = 95e12, confirmations = 2,
}
last_dialog.on_event({ type = "select", key = "block" })
timer_fn()
check(last_dialog.title == "Block Search" and dialog_has_text("⛏ BLOCK"),
      "block overlay via blockhash")
check(dialog_has_text("95.00 T"), "difficulty in T")
check(dialog_has_text("LT:999500"), "block time via btcui_localtime")
last_dialog.on_event({ type = "close" })
check(dialog_has_text("✔ CONFIRMED"), "Esc pops back from the block overlay")
last_dialog.on_event({ type = "close" })

-- 8. Enter on a block bar searches by height (getblockhash → getblock)
rpc_results["getblockhash"] = string.rep("00", 32)
on_select_fn("3", "enter")
check(dialog_has_text("Searching"), "block bar activation starts a search")
timer_fn()
check(last_dialog.title == "Block Search" and dialog_has_text("⛏ BLOCK"),
      "height search shows the block overlay")
check(rpc_calls[#rpc_calls - 3].method == "getblockhash", "height resolved via getblockhash")
last_dialog.on_event({ type = "close" })

-- 9. A 64-hex query that is neither tx nor block reports the TX error, not the
-- block one: on a node without -txindex that message is Core's "Use -txindex…"
-- hint, which is the actionable one.
rpc_results["getblock"] = "ERROR"
on_search_fn(string.rep("ff", 32))
timer_fn()
check(dialog_has_text("rpc failed: getrawtransaction"), "txid error overlay")

-- 10. A query that cannot be a txid still reports the block-lookup error.
on_search_fn("notahash")
timer_fn()
check(dialog_has_text("rpc failed: getblock"), "non-hex query keeps the block error")

-- 11. 'q' inside a dialog quits
last_dialog.on_event({ type = "key", key = "q" })
check(quit_called, "q in dialog quits")

print("ok - " .. checks .. " checks passed (" .. script .. ")")

-- Script-level test for lua/tabs/peers.lua.
--
-- Stubs the btcui_* API (no TUI, no RPC, no threads) and drives the tab through
-- its flows: list refresh, peer detail, disconnect/ban actions, the Added Nodes
-- and Ban List overlays, and the Add Node / Ban-Unban input dialogs.
--
-- Run:  lua tests/lua/test_peers_tab.lua [path/to/peers.lua]
-- Wired into CTest as `test_peers_tab` (see tests/CMakeLists.txt). Exits
-- non-zero on the first failed check.

local script = (arg and arg[1]) or "lua/tabs/peers.lua"

----------------------------------------------------------------------
-- btcui_* stubs
----------------------------------------------------------------------

local rpc_calls   = {}  -- every btcui_rpc invocation, in order
local rpc_results = {}  -- canned result per method ("ERROR" raises)
local last_dialog = nil -- opts of the most recent btcui_dialog call
local on_select_fn                     -- captured btcui_on_select callback
local timer_fn                         -- captured btcui_set_interval callback
local footer = {}                      -- captured footer buttons {label, fn}
local tables = {}                      -- created btcui_table objects
local tab_name
local quit_called = false

local function mktable(opts)
    local t = { opts = opts, rows = {} }
    function t:start_refresh() self.rows = {} end
    function t:finish_refresh() end
    function t:update(key, data) self.rows[tostring(key)] = data end
    function t:remove(key) self.rows[tostring(key)] = nil end
    function t:set_header_info() end
    function t:keys() return {} end
    function t:selected_key() return nil end
    function t:selected_value() return nil end
    tables[#tables + 1] = t
    return t
end

btcui_set_name          = function(n) tab_name = n end
btcui_option            = function(_, d) return d end
btcui_table             = mktable
btcui_on_select         = function(fn) on_select_fn = fn end
btcui_add_footer_button = function(label, fn) footer[#footer + 1] = { label, fn } end
btcui_set_interval      = function(_, fn) timer_fn = fn; return {} end
btcui_wake              = function() end
btcui_now               = function() return 1000000 end
btcui_quit              = function() quit_called = true end
btcui_dialog            = function(opts) last_dialog = opts end
btcui_dialog_close      = function() last_dialog = nil end
btcui_rpc               = function(method, ...)
    rpc_calls[#rpc_calls + 1] = { method = method, ... }
    local r = rpc_results[method]
    if r == "ERROR" then error("rpc failed: " .. method) end
    return r
end

----------------------------------------------------------------------
-- Canned RPC data
----------------------------------------------------------------------

rpc_results["getpeerinfo"] = {
    { id = 3, addr = "1.2.3.4:8333", network = "ipv4", subver = "/Satoshi:27.0/",
      inbound = false, bytessent = 1234567, bytesrecv = 89, version = 70016,
      synced_blocks = 850001, conntime = 999000,
      connection_type = "outbound-full-relay", transport_protocol_type = "v2",
      addr_processed = 12, servicesnames = { "NETWORK", "WITNESS" },
      pingtime = 0.0421, minping = 0.040,
      bip152_hb_from = true, bip152_hb_to = false },
    { id = 12, addr = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd.onion:8333",
      network = "onion", inbound = true, bytessent = 5, bytesrecv = 999999999999 },
}
rpc_results["getaddednodeinfo"] = {
    { addednode = "node1.example.com:8333", addresses = { { connected = true } } },
    { addednode = "node2.example.com:8333", addresses = {} },
}
rpc_results["listbanned"] = {
    { address = "5.6.7.8/32", banned_until = 1003600 },
    { address = "9.9.9.9/32", banned_until = 999999 }, -- already expired
}
rpc_results["disconnectnode"] = true
rpc_results["setban"] = true
rpc_results["addnode"] = true

----------------------------------------------------------------------
-- Checks
----------------------------------------------------------------------

local checks = 0
local function check(cond, msg)
    checks = checks + 1
    if not cond then error("FAIL: " .. msg, 2) end
end

-- Find the last rpc call for `method`, optionally with second parameter `p2`.
local function last_call(method, p2)
    for i = #rpc_calls, 1, -1 do
        local c = rpc_calls[i]
        if c.method == method and (p2 == nil or c[2] == p2) then return c end
    end
end

local function dialog_has_text(needle)
    for _, r in ipairs(last_dialog.rows) do
        local s = type(r) == "table" and (r.text or r.value) or r
        if type(s) == "string" and s:find(needle, 1, true) then return true end
    end
    return false
end

dofile(script)

check(tab_name == "Peers", "tab name set")
check(timer_fn ~= nil, "refresh timer registered")
check(#footer == 2, "footer buttons: [a] add node, [b] ban list")

-- 1. Refresh populates the peers table
timer_fn()
local pt = tables[1]
check(pt.rows["3"] ~= nil and pt.rows["12"] ~= nil, "peer rows present")
check(pt.rows["3"].io.value == "out" and pt.rows["3"].io.color == "green", "outbound io cell")
check(pt.rows["12"].io.value == "in" and pt.rows["12"].io.color == "cyan", "inbound io cell")
check(pt.rows["3"].ping:find("42.1", 1, true), "ping in ms")
check(pt.rows["3"].recv:find("89 B", 1, true), "recv bytes")
check(pt.rows["3"].sent:find("1.2 MB", 1, true), "sent bytes")
check(pt.rows["3"].height:find("850'001", 1, true), "grouped height")
check(pt.rows["12"].ping:find("—", 1, true), "missing ping renders as dash")

-- 2. Enter on a peer opens the detail dialog
on_select_fn("3", "enter")
check(last_dialog and last_dialog.title == "Peer 3", "detail dialog open")
check(#last_dialog.buttons == 2, "Disconnect / Ban buttons")
check(dialog_has_text("outbound"), "direction row")
check(dialog_has_text("NETWORK, WITNESS"), "services row")
check(dialog_has_text("42.1 ms"), "ping row")

-- 3. Disconnect (button 1) → working overlay → RPC on next tick → result
last_dialog.on_event({ type = "button", index = 1, label = "Disconnect" })
check(last_dialog.title == "Peer Action" and last_dialog.closable == false,
      "working overlay not closable")
timer_fn()
check(last_call("disconnectnode")[1] == "1.2.3.4:8333", "disconnectnode issued")
check(last_dialog.closable ~= false and dialog_has_text("Disconnected"), "result overlay")
last_dialog.on_event({ type = "close" })

-- 4. Ban via key 'b' on the onion peer: port is stripped for setban
on_select_fn("12", "enter")
last_dialog.on_event({ type = "key", key = "b" })
timer_fn()
local ban = last_call("setban", "add")
check(ban[1] == "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd.onion",
      "setban host without port")
check(ban[3] == 86400, "setban passes the 24h the button advertises")
check(dialog_has_text("Banned"), "ban result overlay")
last_dialog.on_event({ type = "close" })

-- 5. Added Nodes overlay: lazy fetch, remove, Add Node input dialog
footer[1][2]() -- [a] add node
check(last_dialog.title == "Added Nodes" and dialog_has_text("Loading"), "loading state")
timer_fn()
check(last_dialog.rows[1].key == "node1.example.com:8333", "added-node item")
check(last_dialog.rows[1].spans[1].color == "green", "connected dot")
check(last_dialog.rows[2].spans[1].color == "gray", "disconnected dot")

last_dialog.on_event({ type = "select", key = "node2.example.com:8333" })
timer_fn()
check(last_call("addnode", "remove")[1] == "node2.example.com:8333", "addnode remove issued")
check(last_dialog.title == "Added Nodes", "list re-rendered after remove")

last_dialog.on_event({ type = "key", key = "a" })
check(last_dialog.title == "Add Node" and last_dialog.input ~= nil, "Add Node input dialog")
check(last_dialog.choice.options[1] == "onetry" and last_dialog.choice.options[2] == "add",
      "command choice")
last_dialog.on_event({ type = "submit", text = "  seed.example.org:8333  ", choice = 2 })
check(last_dialog.input == nil and dialog_has_text("Connecting"), "progress dialog")
timer_fn()
local an = last_call("addnode", "add")
check(an[1] == "seed.example.org:8333", "addnode with trimmed address")
check(dialog_has_text("✓"), "addnode success")
last_dialog.on_event({ type = "close" })
check(last_dialog.title == "Added Nodes", "Esc returns to Added Nodes")
last_dialog.on_event({ type = "close" }) -- back to the peers list

-- 6. Ban List overlay: remaining time, unban, Ban/Unban input dialog
footer[2][2]() -- [b] ban list
check(last_dialog.title == "Ban List", "ban list dialog")
timer_fn()
check(last_dialog.rows[1].key == "5.6.7.8/32", "ban entry")
check(last_dialog.rows[1].right == "1h 0m", "remaining ban time")
check(last_dialog.rows[2].right == "expired", "expired entry")

last_dialog.on_event({ type = "select", key = "5.6.7.8/32" })
timer_fn()
check(last_call("setban", "remove")[1] == "5.6.7.8/32", "unban issued")

last_dialog.on_event({ type = "key", key = "b" })
check(last_dialog.title == "Ban / Unban Node" and last_dialog.input ~= nil,
      "ban/unban input dialog")
rpc_results["setban"] = "ERROR"
last_dialog.on_event({ type = "submit", text = "10.0.0.1", choice = 1 })
timer_fn()
check(dialog_has_text("✗"), "setban error surfaced in dialog")
last_dialog.on_event({ type = "close" })
check(last_dialog.title == "Ban List", "Esc returns to Ban List")
last_dialog.on_event({ type = "close" })

-- 7. getpeerinfo failure keeps the previous rows; 'q' inside a dialog quits
rpc_results["getpeerinfo"] = "ERROR"
timer_fn()
check(pt.rows["3"] ~= nil, "rows kept on rpc error")
on_select_fn("3", "enter")
last_dialog.on_event({ type = "key", key = "q" })
check(quit_called, "q in dialog quits")

print("ok - " .. checks .. " checks passed (" .. script .. ")")

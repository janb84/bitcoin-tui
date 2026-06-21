-- Network — Lua port of the built-in C++ Network tab (src/tabs/network.cpp).
--
-- Shows connection counts, node client/protocol/relay fee, and a soft-fork
-- tracking table fed by getdeploymentinfo. Built entirely from the composable
-- btcui_summary / btcui_table components so it reuses the same section-box
-- rendering and footer/refresh behaviour as the rest of the UI.
--
-- Load with: --tab lua/tabs/network.lua
-- Optional refresh interval (seconds): --tab lua/tabs/network.lua,interval=5

-- Lua 5.5 strict globals: a typo in any name below is caught at load time.
global btcui_option, btcui_rpc, btcui_set_interval, btcui_set_name,
       btcui_summary, btcui_table, ipairs, math, pairs, pcall, string, table,
       tonumber, tostring

btcui_set_name("Network")

local REFRESH = tonumber(btcui_option("interval", "5")) or 5

----------------------------------------------------------------------
-- Formatting helpers (mirror src/format.hpp)
----------------------------------------------------------------------

-- Group digits from the right with a separator, e.g. 1234567 -> 1'234'567.
local function group_digits(n, sep)
    local s   = tostring(math.floor(n))
    local out = ""
    local cnt = 0
    for i = #s, 1, -1 do
        out = s:sub(i, i) .. out
        cnt = cnt + 1
        if cnt % 3 == 0 and i > 1 then out = sep .. out end
    end
    return out
end

local function fmt_height(n) return group_digits(n, "'") end

-- relayfee is BTC/kvB; show as sat/vB (matches fmt_satsvb in src/format.hpp).
local function fmt_satsvb(btc_per_kvb)
    return string.format("%.1f sat/vB", (btc_per_kvb or 0) * 1e5)
end

local function yesno(v, yes_color, no_color)
    return { value = v and "yes" or "no", color = v and yes_color or no_color }
end

----------------------------------------------------------------------
-- Panels — Network Status | Node side by side, Softfork Tracking below
-- (two consecutive summaries render side-by-side; a table flushes the run
--  and starts a new full-width row, mirroring the C++ tab's layout)
----------------------------------------------------------------------

local status_panel = btcui_summary({
    title = "Network Status",
    fields = {
        { name = "active",   label = "Active" },
        { name = "peers",    label = "Peers" },
        { name = "inbound",  label = "Inbound" },
        { name = "outbound", label = "Outbound" },
    },
})

local node_panel = btcui_summary({
    title = "Node",
    fields = {
        { name = "client",   label = "Client" },
        { name = "protocol", label = "Protocol" },
        { name = "relayfee", label = "Relay fee" },
    },
})

local fork_table = btcui_table({
    key = "name",
    title = "Softfork Tracking",
    columns = {
        { name = "name",   header = "Name" },
        { name = "type",   header = "Type" },
        { name = "status", header = "Status" },
        { name = "height", header = "Height" },
    },
})

----------------------------------------------------------------------
-- Refresh
----------------------------------------------------------------------

-- Status colour mirrors render_network() in network.cpp:
--   active -> green, locked_in -> yellow, started -> cyan, else -> gray
local function status_color(active, status)
    if active               then return "green" end
    if status == "locked_in" then return "yellow" end
    if status == "started"   then return "cyan" end
    return "gray"
end

local function refresh()
    -- Network Status + Node panels (from getnetworkinfo).
    -- The C++ tab read these from the shared AppState populated by the global
    -- poll thread; a Lua tab has no access to AppState, so we fetch directly —
    -- the same approach dashboard.lua takes.
    local oknet, net = pcall(btcui_rpc, "getnetworkinfo")
    if oknet and net then
        status_panel:set({
            active   = yesno(net.networkactive, "green", "red"),
            peers    = tostring(net.connections or 0),
            inbound  = tostring(net.connections_in or 0),
            outbound = tostring(net.connections_out or 0),
        })
        node_panel:set({
            client   = net.subversion or "",
            protocol = tostring(net.protocolversion or 0),
            relayfee = fmt_satsvb(net.relayfee),
        })
    end

    -- Softfork tracking (from getdeploymentinfo).
    local ok, info = pcall(btcui_rpc, "getdeploymentinfo")
    local dep = (ok and info and info.deployments) or nil

    fork_table:start_refresh()
    if dep then
        -- Collect deployments into an array so we can sort them the same way
        -- the C++ tab does: active first, then by name ascending.
        local forks = {}
        for name, val in pairs(dep) do
            local b9     = val.bip9 or {}
            local status = (b9.status and b9.status ~= "") and b9.status or (val.type or "")
            forks[#forks + 1] = {
                name   = name,
                type   = val.type or "",
                status = status,
                active = val.active == true,
                height = val.height,
            }
        end
        table.sort(forks, function(a, b)
            if a.active ~= b.active then return a.active end
            return a.name < b.name
        end)
        for _, f in ipairs(forks) do
            local height_str = (f.height and f.height >= 0) and fmt_height(f.height) or "—"
            fork_table:update(f.name, {
                name   = f.name,
                type   = { value = f.type,   color = "gray" },
                status = { value = f.status, color = status_color(f.active, f.status) },
                height = { value = height_str, color = "gray" },
            })
        end
        -- Clear any previous "unavailable" banner now that we have data.
        fork_table:set_header_info("")
    else
        fork_table:set_header_info({
            value = "(unavailable — node may not support getdeploymentinfo)",
            color = "gray",
        })
    end
    fork_table:finish_refresh()
end

btcui_set_interval(REFRESH, refresh)

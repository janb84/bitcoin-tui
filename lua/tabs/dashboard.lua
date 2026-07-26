-- Dashboard — blockchain, network and mempool summary at a glance.
--
-- Demonstrates summary panels, the 2+1 layout (new_row), and progress bars
-- via btcui_gauge(). Reads getblockchaininfo / getnetworkinfo / getmempoolinfo,
-- plus getnetworkhashps for the hashrate Core itself reports.
--
-- Load with: --tab lua/tabs/dashboard.lua (or enable it from the Settings tab)
-- Optional refresh interval (seconds): --tab lua/tabs/dashboard.lua,interval=2

-- Lua 5.5 strict globals: a typo in any name below is caught at load time.
global btcui_gauge, btcui_option, btcui_rpc, btcui_set_interval, btcui_set_name,
       btcui_summary, ipairs, math, pcall, string, tonumber, tostring, type

btcui_set_name("Dashboard")

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
local function fmt_int(n)    return group_digits(n, ",") end

-- Scale a value down through a list of {threshold, suffix} steps. Values too
-- small for the smallest step fall back to significant digits rather than fixed
-- decimals, so regtest's 4.7e-10 difficulty reads as itself instead of "0.00".
local function fmt_scaled(v, decimals, steps, base_suffix)
    for _, step in ipairs(steps) do
        if v >= step[1] then
            return string.format("%." .. decimals .. "f %s", v / step[1], step[2])
        end
    end
    if v > 0 and v < 0.01 then
        return string.format("%.3g%s", v, base_suffix)
    end
    return string.format("%." .. decimals .. "f%s", v, base_suffix)
end

local function fmt_difficulty(d)
    return fmt_scaled(d, 2, {
        { 1e18, "E" }, { 1e15, "P" }, { 1e12, "T" }, { 1e9, "G" },
    }, "")
end

local function fmt_hashrate(h)
    return fmt_scaled(h, 2, {
        { 1e21, "ZH/s" }, { 1e18, "EH/s" }, { 1e15, "PH/s" }, { 1e12, "TH/s" },
        { 1e9, "GH/s" }, { 1e6, "MH/s" }, { 1e3, "kH/s" },
    }, " H/s")
end

local function fmt_bytes(b)
    if b >= 1e9 then return string.format("%.1f GB", b / 1e9) end
    if b >= 1e6 then return string.format("%.1f MB", b / 1e6) end
    if b >= 1e3 then return string.format("%.1f KB", b / 1e3) end
    return string.format("%d B", math.floor(b))
end

-- relayfee / mempoolminfee are BTC/kvB; show as sat/vB.
local function fmt_satsvb(btc_per_kvb)
    return string.format("%.1f sat/vB", (btc_per_kvb or 0) * 1e5)
end

local function fmt_btc(v)
    return string.format("%.8f BTC", v or 0)
end

local function yesno(v, yes_color, no_color)
    return { value = v and "yes" or "no", color = v and yes_color or no_color }
end

----------------------------------------------------------------------
-- Panels — Blockchain | Network side by side, Mempool full-width below
----------------------------------------------------------------------

local blockchain_panel = btcui_summary({
    title = "Blockchain",
    fields = {
        { name = "chain",      label = "Chain" },
        { name = "height",     label = "Height" },
        { name = "headers",    label = "Headers" },
        { name = "difficulty", label = "Difficulty" },
        { name = "hashrate",   label = "Hash Rate" },
        { name = "sync",       label = "Sync" },
        { name = "ibd",        label = "IBD" },
        { name = "pruned",     label = "Pruned" },
    },
})

local network_panel = btcui_summary({
    title = "Network",
    fields = {
        { name = "active",      label = "Active" },
        { name = "connections", label = "Connections" },
        { name = "conn_in",     label = "  In" },
        { name = "conn_out",    label = "  Out" },
        { name = "client",      label = "Client" },
        { name = "protocol",    label = "Protocol" },
        { name = "relayfee",    label = "Relay fee" },
    },
})

local mempool_panel = btcui_summary({
    title = "Mempool",
    new_row = true, -- break the side-by-side run: render full-width below
    fields = {
        { name = "transactions", label = "Transactions" },
        { name = "vsize",        label = "Virtual size" },
        { name = "total_fees",   label = "Total fees" },
        -- mempoolminfee (dynamic eviction floor), not the minrelaytxfee setting.
        { name = "min_fee",      label = "Mempool min fee" },
        { name = "memory",       label = "Memory usage" },
    },
})

----------------------------------------------------------------------
-- Refresh
----------------------------------------------------------------------

-- getnetworkhashps measures work over the last 120 blocks. It is not in every
-- node's reach (very old nodes lack it), so fall back to the difficulty estimate
-- rather than blanking the field.
local function hashps(difficulty)
    local ok, hps = pcall(btcui_rpc, "getnetworkhashps")
    if ok and type(hps) == "number" then return hps end
    return (difficulty or 0) * 4294967296.0 / 600.0
end

btcui_set_interval(REFRESH, function()
    local bc = btcui_rpc("getblockchaininfo")
    if bc then
        local chain    = bc.chain or "—"
        local progress = bc.verificationprogress or 0
        blockchain_panel:set({
            chain      = { value = chain == "main" and "mainnet" or chain,
                           color = chain == "main" and "green" or "yellow" },
            height     = fmt_height(bc.blocks or 0),
            headers    = fmt_height(bc.headers or 0),
            difficulty = fmt_difficulty(bc.difficulty or 0),
            -- Core's own estimate, so the figure matches `bitcoin-cli
            -- getnetworkhashps` / getmininginfo. Deriving it from difficulty
            -- (difficulty * 2^32 / 600) gives the *expected* rate at the current
            -- target, which drifts from what the network actually produced.
            hashrate   = fmt_hashrate(hashps(bc.difficulty)),
            sync       = btcui_gauge(progress, { color = progress >= 1.0 and "green" or "yellow" }),
            ibd        = yesno(bc.initialblockdownload, "yellow", "green"),
            pruned     = { value = bc.pruned and "yes" or "no" },
        })
    end

    local net = btcui_rpc("getnetworkinfo")
    if net then
        network_panel:set({
            active      = yesno(net.networkactive, "green", "red"),
            connections = tostring(net.connections or 0),
            conn_in     = tostring(net.connections_in or 0),
            conn_out    = tostring(net.connections_out or 0),
            client      = net.subversion or "",
            protocol    = tostring(net.protocolversion or 0),
            relayfee    = fmt_satsvb(net.relayfee),
        })
    end

    local mp = btcui_rpc("getmempoolinfo")
    if mp then
        local usage  = mp.usage or 0
        local maxmem = mp.maxmempool or 300000000
        local frac   = maxmem > 0 and usage / maxmem or 0
        local mcolor = frac > 0.8 and "red" or (frac > 0.5 and "yellow" or "cyan")
        mempool_panel:set({
            transactions = fmt_int(mp.size or 0),
            vsize        = fmt_bytes(mp.bytes or 0),
            total_fees   = fmt_btc(mp.total_fee),
            min_fee      = fmt_satsvb(mp.mempoolminfee),
            memory       = btcui_gauge(frac, {
                color  = mcolor,
                prefix = fmt_bytes(usage) .. " / " .. fmt_bytes(maxmem),
            }),
        })
    end
end)

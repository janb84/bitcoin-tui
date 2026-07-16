-- Mempool — Lua port of the built-in C++ Mempool tab (src/tabs/mempool.cpp).
--
-- Built from the composable components:
--   • Mempool stats   — btcui_summary fed from getmempoolinfo.
--   • Recent Blocks   — btcui_blocks fill-bar columns fed from getblockstats;
--                       ←/→ selects a block, Enter looks it up.
--   • Search overlays — btcui_dialog modals for the transaction / block search
--                       (Searching…, block, mempool tx, confirmed tx, error),
--                       plus the Inputs and Outputs sub-overlays with the
--                       history stack (Esc pops back to the previous result).
--
-- This tab registers btcui_on_search, so the global "/" search bar and
-- btcui_search() calls from other tabs are routed here. btcui_rpc can only run
-- inside the refresh timer (it yields the coroutine), so search requests set a
-- pending flag and btcui_wake() the timer, which performs the lookups.

-- Lua 5.5 strict globals: a typo in any name below is caught at load time.
global btcui_blocks, btcui_dialog, btcui_gauge, btcui_key_hint, btcui_localtime,
       btcui_now, btcui_on_search, btcui_on_select, btcui_option, btcui_quit,
       btcui_rpc, btcui_set_interval, btcui_set_name, btcui_summary, btcui_wake,
       ipairs, math, pairs, pcall, string, tonumber, tostring, type

btcui_set_name("Mempool")

local REFRESH = tonumber(btcui_option("interval", "5")) or 5

local PANEL_W    = 70 -- main search overlay width
local IO_W       = 84 -- inputs/outputs sub-overlay width (full 64-char txids)
local MAX_BLOCKS = 20 -- block stats fetched/kept (the panel clips to the terminal)
local MAX_WEIGHT = 4000000

----------------------------------------------------------------------
-- Formatting helpers (mirror src/format.hpp)
----------------------------------------------------------------------

-- Group digits from the right with a separator, e.g. 1234567 -> 1,234,567.
local function group_digits(n, sep)
    local s   = tostring(math.floor(n or 0))
    local out = ""
    local cnt = 0
    for i = #s, 1, -1 do
        out = s:sub(i, i) .. out
        cnt = cnt + 1
        if cnt % 3 == 0 and i > 1 then out = sep .. out end
    end
    return out
end

local function fmt_int(n)    return group_digits(n, ",") end
local function fmt_height(n) return group_digits(n, "'") end

local function fmt_bytes(b)
    b = b or 0
    if b >= 1e9 then return string.format("%.1f GB", b / 1e9) end
    if b >= 1e6 then return string.format("%.1f MB", b / 1e6) end
    if b >= 1e3 then return string.format("%.1f KB", b / 1e3) end
    return string.format("%d B", math.floor(b))
end

local function fmt_btc(v)
    return string.format("%.8f BTC", v or 0)
end

-- relayfee / mempoolminfee are BTC/kvB; show as sat/vB.
local function fmt_satsvb(btc_per_kvb)
    return string.format("%.1f sat/vB", (btc_per_kvb or 0) * 1e5)
end

local function fmt_age(secs)
    if secs < 60 then return secs .. "s" end
    if secs < 3600 then return math.floor(secs / 60) .. "m " .. (secs % 60) .. "s" end
    return math.floor(secs / 3600) .. "h " .. math.floor((secs % 3600) / 60) .. "m"
end

local function fmt_time_ago(ts)
    local diff = btcui_now() - ts
    if diff < 0 then return "just now" end
    if diff < 60 then return diff .. "s ago" end
    if diff < 3600 then return math.floor(diff / 60) .. "m ago" end
    if diff < 86400 then return math.floor(diff / 3600) .. "h ago" end
    return math.floor(diff / 86400) .. "d ago"
end

local function ellipsize_middle(s, max_len, prefix, suffix)
    if #s <= max_len then return s end
    return s:sub(1, prefix) .. "…" .. s:sub(#s - suffix + 1)
end

local function trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end

-- Word-wrap `msg` to `width` columns (for long RPC error messages).
local function wrap(msg, width)
    local lines, cur = {}, ""
    for word in tostring(msg):gmatch("%S+") do
        if #cur > 0 and #cur + 1 + #word > width then
            lines[#lines + 1] = cur
            cur = ""
        end
        cur = (#cur > 0) and (cur .. " " .. word) or word
    end
    if #cur > 0 then lines[#lines + 1] = cur end
    if #lines == 0 then lines[1] = "" end
    return lines
end

-- Longest printable ASCII run (≥4 chars, '/' excluded) in the coinbase
-- scriptSig hex — the usual miner tag.
local function extract_miner(hex)
    local best, run = "", ""
    for i = 1, #hex - 1, 2 do
        local b = tonumber(hex:sub(i, i + 1), 16) or 0
        if b >= 0x20 and b < 0x7f and b ~= 0x2f then
            run = run .. string.char(b)
        else
            if #run >= 4 and #run > #best then best = run end
            run = ""
        end
    end
    if #run >= 4 and #run > #best then best = run end
    if #best > 24 then best = best:sub(1, 24) end
    return best ~= "" and best or "—"
end

local function is_height(q) return q:match("^%d+$") ~= nil end

local function abbrev(q) return ellipsize_middle(q, 40, 20, 20) end

-- "  Label        : value" row with the label padded so colons align.
local function lv(label, value, color)
    return { label = string.format("  %-13s: ", label), value = value, color = color }
end

----------------------------------------------------------------------
-- Panels — Mempool stats summary + Recent Blocks bars
----------------------------------------------------------------------

local mempool_panel = btcui_summary({
    title = "Mempool",
    fields = {
        { name = "transactions", label = "Transactions" },
        { name = "vsize",        label = "Virtual size" },
        { name = "total_fees",   label = "Total fees" },
        { name = "min_relay",    label = "Min relay fee" },
        { name = "memory",       label = "Memory usage" },
    },
})

local blocks_panel = btcui_blocks({ title = "Recent Blocks" })

----------------------------------------------------------------------
-- State
----------------------------------------------------------------------

local refresh_timer          -- assigned by btcui_set_interval (used by btcui_wake)
local view = "none"          -- none | searching | result | inputs | outputs
local current = nil          -- search result currently shown
local history = {}           -- stack of { r = result, sub = "result"|"inputs" }
local pending_search = nil   -- query queued for the refresh timer

local tip = 0                -- chain tip height (from getblockchaininfo)
local stats_cache = {}       -- height -> { txs, size, weight, time }

----------------------------------------------------------------------
-- Search overlays — btcui_dialog modals
----------------------------------------------------------------------

local show_result, show_searching  -- forward declarations

-- Esc: restore the previous result from the history stack, or dismiss.
local function pop_or_dismiss()
    local e = history[#history]
    if e then
        history[#history] = nil
        if e.sub == "inputs" then
            -- re-enter via the tx result so `current` is restored too
            show_result(e.r, "inputs")
        else
            show_result(e.r)
        end
    else
        current = nil
        view = "none"
    end
end

-- Start a lookup. `push_entry` is the history entry to stack for Esc-back
-- (nil = a fresh search: the history is cleared, like the global search bar).
local function do_search(query, push_entry)
    query = trim(tostring(query or ""))
    if query == "" or pending_search then return end
    if push_entry then
        history[#history + 1] = push_entry
    else
        history = {}
    end
    current = nil
    pending_search = query
    show_searching(query)
    btcui_wake(refresh_timer)
end

show_searching = function(query)
    view = "searching"
    btcui_dialog({
        title       = "Transaction Search",
        right_label = abbrev(query),
        width       = PANEL_W,
        rows        = { { text = "  Searching…", color = "yellow" } },
        hint        = "[Esc] dismiss",
        on_event    = function(ev)
            if ev.type == "close" then
                pop_or_dismiss()
            elseif ev.type == "key" and ev.key == "q" then
                btcui_quit()
            end
        end,
    })
end

-- Inputs sub-overlay: full txid:vout per input, Enter looks the input tx up.
local function show_inputs(r)
    view = "inputs"
    local rows = {}
    for i, vin in ipairs(r.vin) do
        local idx = { text = "  [" .. (i - 1) .. "] ", color = "gray" }
        if vin.coinbase then
            rows[#rows + 1] = { spans = { idx, { text = "coinbase", color = "gray" } } }
        else
            rows[#rows + 1] = {
                key   = tostring(i),
                spans = { idx, { text = vin.txid .. ":" .. vin.vout } },
            }
        end
    end
    btcui_dialog({
        title       = "Inputs (" .. #r.vin .. ")",
        right_label = abbrev(r.query),
        width       = IO_W,
        rows        = rows,
        hint        = "[↑/↓] navigate  [⏎] lookup  [Esc] back",
        on_event    = function(ev)
            if ev.type == "select" then
                local vin = r.vin[tonumber(ev.key)]
                if vin and not vin.coinbase then
                    do_search(vin.txid, { r = r, sub = "inputs" })
                end
            elseif ev.type == "close" then
                show_result(r)
            elseif ev.type == "key" and ev.key == "q" then
                btcui_quit()
            end
        end,
    })
end

-- Outputs sub-overlay: value + address (or script type), display-only.
local function show_outputs(r)
    view = "outputs"
    local rows = {}
    for i, out in ipairs(r.vout) do
        local spans = {
            { text = "  [" .. (i - 1) .. "] ", color = "gray" },
            { text = fmt_btc(out.value) },
        }
        if out.address ~= "" then
            spans[#spans + 1] = { text = "  " }
            spans[#spans + 1] = { address = out.address }
        elseif out.stype ~= "" then
            spans[#spans + 1] = { text = "  [" .. out.stype .. "]" }
        end
        -- keyed rows are navigable, so long lists scroll; Enter is a no-op
        rows[#rows + 1] = { key = tostring(i), spans = spans }
    end
    btcui_dialog({
        title       = "Outputs (" .. #r.vout .. ")",
        right_label = abbrev(r.query),
        width       = IO_W,
        rows        = rows,
        hint        = "[↑/↓] navigate  [Esc] back",
        on_event    = function(ev)
            if ev.type == "close" then
                show_result(r)
            elseif ev.type == "key" and ev.key == "q" then
                btcui_quit()
            end
        end,
    })
end

-- The main result overlay; `sub` re-opens a sub-overlay (history restore).
show_result = function(r, sub)
    current = r
    if sub == "inputs" then return show_inputs(r) end
    view = "result"

    local title = r.kind == "block" and "Block Search" or "Transaction Search"
    local hint  = "[Esc] dismiss"
    local rows  = {}

    if r.kind == "block" then
        local age = r.time > 0 and math.max(0, btcui_now() - r.time) or 0
        rows = {
            { text = "  ⛏ BLOCK", color = "cyan", bold = true },
            lv("Height", fmt_height(r.height)),
            lv("Hash", ellipsize_middle(r.hash, 48, 4, 44)),
            lv("Time", r.time > 0 and btcui_localtime(r.time) or "—"),
            lv("Age", r.time > 0 and fmt_age(age) or "—"),
            lv("Transactions", fmt_int(r.ntx)),
            lv("Size", fmt_int(r.size) .. " B"),
            lv("Weight", fmt_int(r.weight) .. " WU"),
            lv("Difficulty", string.format("%.2f T", (r.difficulty or 0) / 1e12)),
            lv("Miner", r.miner),
            lv("Confirmations", fmt_int(r.confirmations)),
        }
    elseif r.kind == "mempool" then
        local age = math.max(0, btcui_now() - (r.entry_time or 0))
        rows = {
            { text = "  ● MEMPOOL", color = "yellow", bold = true },
            lv("Fee", fmt_btc(r.fee), "green"),
            lv("Fee rate", string.format("%.1f sat/vB", r.fee_rate or 0)),
            lv("vsize", fmt_int(r.vsize) .. " vB"),
            lv("Weight", fmt_int(r.weight) .. " WU"),
            lv("Ancestors", fmt_int(r.ancestors)),
            lv("Descendants", fmt_int(r.descendants)),
            lv("In mempool", fmt_age(age)),
        }
    elseif r.kind == "confirmed" then
        local age = r.blocktime > 0 and math.max(0, btcui_now() - r.blocktime) or 0
        hint = "[↑/↓] navigate  [⏎] open  [Esc] dismiss"
        rows[#rows + 1] = { text = "  ✔ CONFIRMED", color = "green", bold = true }
        rows[#rows + 1] = lv("Confirmations", fmt_int(r.confirmations))
        rows[#rows + 1] = {
            key   = "block",
            spans = {
                { text = "  Block #      : ", color = "gray" },
                { text = r.block_height >= 0 and fmt_height(r.block_height) or "—",
                  color = "cyan" },
            },
        }
        rows[#rows + 1] = lv("Block hash", ellipsize_middle(r.blockhash, 48, 4, 44))
        rows[#rows + 1] = lv("Block age", r.blocktime > 0 and fmt_age(age) or "—")
        rows[#rows + 1] = lv("vsize", fmt_int(r.vsize) .. " vB")
        rows[#rows + 1] = lv("Weight", fmt_int(r.weight) .. " WU")
        if #r.vin > 0 then
            rows[#rows + 1] = {
                key   = "inputs",
                spans = {
                    { text = "  Inputs       : ", color = "gray" },
                    { text = tostring(#r.vin), color = "cyan" },
                },
            }
        end
        if #r.vout > 0 then
            rows[#rows + 1] = {
                key   = "outputs",
                spans = {
                    { text = "  Outputs      : ", color = "gray" },
                    { text = tostring(#r.vout), color = "cyan" },
                },
            }
        end
        rows[#rows + 1] = lv("Total out", fmt_btc(r.total_out), "green")
    else -- error
        for _, line in ipairs(wrap(r.error or "not found", PANEL_W - 6)) do
            rows[#rows + 1] = { text = "  " .. line, color = "red" }
        end
    end

    btcui_dialog({
        title       = title,
        right_label = abbrev(r.query),
        width       = PANEL_W,
        rows        = rows,
        hint        = hint,
        on_event    = function(ev)
            if ev.type == "select" then
                if ev.key == "block" and r.blockhash ~= "" then
                    do_search(r.blockhash, { r = r, sub = "result" })
                elseif ev.key == "inputs" then
                    show_inputs(r)
                elseif ev.key == "outputs" then
                    show_outputs(r)
                end
            elseif ev.type == "close" then
                pop_or_dismiss()
            elseif ev.type == "key" and ev.key == "q" then
                btcui_quit()
            end
        end,
    })
end

----------------------------------------------------------------------
-- Search lookups — run inside the refresh timer (btcui_rpc yields)
----------------------------------------------------------------------

-- getblock (verbosity 1) + miner tag from the coinbase scriptSig.
local function fetch_block(hash)
    local ok, blk = pcall(btcui_rpc, "getblock", hash, 1)
    if not ok or type(blk) ~= "table" then
        return { kind = "error", error = tostring(blk) }
    end
    local r = {
        kind          = "block",
        hash          = blk.hash or hash,
        height        = blk.height or 0,
        time          = blk.time or 0,
        ntx           = blk.nTx or 0,
        size          = blk.size or 0,
        weight        = blk.weight or 0,
        difficulty    = blk.difficulty or 0,
        confirmations = blk.confirmations or 0,
        miner         = "—",
    }
    if type(blk.tx) == "table" and blk.tx[1] then
        local ok2, cb = pcall(btcui_rpc, "getrawtransaction", blk.tx[1], true)
        if ok2 and type(cb) == "table" and type(cb.vin) == "table"
           and cb.vin[1] and cb.vin[1].coinbase then
            r.miner = extract_miner(cb.vin[1].coinbase)
        end
    end
    return r
end

-- Height → block; else mempool entry → confirmed tx (txindex) → block hash.
local function run_search(query)
    if is_height(query) then
        local ok, hash = pcall(btcui_rpc, "getblockhash", tonumber(query))
        if not ok then return { kind = "error", error = tostring(hash) } end
        return fetch_block(hash)
    end

    local ok, entry = pcall(btcui_rpc, "getmempoolentry", query)
    if ok and type(entry) == "table" then
        local fee   = (type(entry.fees) == "table" and entry.fees.base) or entry.fee or 0
        local vsize = entry.vsize or 0
        return {
            kind        = "mempool",
            fee         = fee,
            fee_rate    = vsize > 0 and fee * 1e8 / vsize or 0,
            vsize       = vsize,
            weight      = entry.weight or 0,
            ancestors   = entry.ancestorcount or 0,
            descendants = entry.descendantcount or 0,
            entry_time  = entry.time or 0,
        }
    end

    local ok2, tx = pcall(btcui_rpc, "getrawtransaction", query, true)
    if ok2 and type(tx) == "table" then
        local r = {
            kind          = "confirmed",
            vsize         = tx.vsize or 0,
            weight        = tx.weight or 0,
            blockhash     = tx.blockhash or "",
            confirmations = tx.confirmations or 0,
            blocktime     = tx.blocktime or 0,
            block_height  = -1,
            vin           = {},
            vout          = {},
            total_out     = 0,
        }
        if tip > 0 and r.confirmations > 0 then
            r.block_height = tip - r.confirmations + 1
        end
        for _, inp in ipairs(type(tx.vin) == "table" and tx.vin or {}) do
            if inp.coinbase then
                r.vin[#r.vin + 1] = { coinbase = true }
            else
                r.vin[#r.vin + 1] = { txid = inp.txid or "", vout = inp.vout or 0 }
            end
        end
        for _, out in ipairs(type(tx.vout) == "table" and tx.vout or {}) do
            local v = { value = out.value or 0, address = "", stype = "" }
            if type(out.scriptPubKey) == "table" then
                v.stype   = out.scriptPubKey.type or ""
                v.address = out.scriptPubKey.address or ""
            end
            r.total_out = r.total_out + v.value
            r.vout[#r.vout + 1] = v
        end
        return r
    end

    return fetch_block(query)
end

----------------------------------------------------------------------
-- Refresh — the only place btcui_rpc may run (it yields the coroutine)
----------------------------------------------------------------------

local function update_blocks_panel()
    local bars = {}
    for i = 0, MAX_BLOCKS - 1 do
        local h  = tip - i
        local st = h >= 0 and stats_cache[h] or nil
        if not st then break end
        bars[#bars + 1] = {
            key   = tostring(h),
            label = fmt_height(h),
            fill  = math.min(1, (st.weight or 0) / MAX_WEIGHT),
            lines = {
                fmt_int(st.txs) .. " tx",
                fmt_bytes(st.size),
                st.time > 0 and fmt_time_ago(st.time) or "",
            },
        }
    end
    blocks_panel:set(bars)
end

local function refresh()
    -- 1. Pending search queued by btcui_on_search / row activations.
    if pending_search then
        local q = pending_search
        local r = run_search(q)
        r.query = q
        pending_search = nil
        -- Only surface the result if the Searching… overlay wasn't dismissed.
        if view == "searching" then show_result(r) end
    end

    -- 2. Mempool stats.
    local ok, mp = pcall(btcui_rpc, "getmempoolinfo")
    if ok and type(mp) == "table" then
        local usage  = mp.usage or 0
        local maxmem = mp.maxmempool or 300000000
        local frac   = maxmem > 0 and usage / maxmem or 0
        local mcolor = frac > 0.8 and "red" or (frac > 0.5 and "yellow" or "cyan")
        mempool_panel:set({
            transactions = fmt_int(mp.size or 0),
            vsize        = fmt_bytes(mp.bytes or 0),
            total_fees   = fmt_btc(mp.total_fee),
            min_relay    = fmt_satsvb(mp.mempoolminfee),
            memory       = btcui_gauge(frac, {
                color  = mcolor,
                prefix = fmt_bytes(usage) .. " / " .. fmt_bytes(maxmem),
            }),
        })
    end

    -- 3. Recent blocks: fetch stats only for heights not yet cached.
    local ok2, bc = pcall(btcui_rpc, "getblockchaininfo")
    if ok2 and type(bc) == "table" then
        tip = bc.blocks or 0
        for i = 0, MAX_BLOCKS - 1 do
            local h = tip - i
            if h < 0 then break end
            if not stats_cache[h] then
                local ok3, bs = pcall(btcui_rpc, "getblockstats", h,
                                      { "height", "txs", "total_size", "total_weight", "time" })
                if not ok3 or type(bs) ~= "table" then break end
                stats_cache[h] = {
                    txs    = bs.txs or 0,
                    size   = bs.total_size or 0,
                    weight = bs.total_weight or 0,
                    time   = bs.time or 0,
                }
            end
        end
        for h in pairs(stats_cache) do
            if h < tip - MAX_BLOCKS + 1 or h > tip then stats_cache[h] = nil end
        end
        update_blocks_panel()
    end
end

----------------------------------------------------------------------
-- Interaction
----------------------------------------------------------------------

-- Enter on a selected block bar: look the block up by height.
btcui_on_select(function(key)
    do_search(key)
end)

-- Global search bar ("/") and btcui_search() from other tabs land here.
btcui_on_search(function(query)
    do_search(query)
end)

btcui_key_hint("[↓] select blocks")

refresh_timer = btcui_set_interval(REFRESH, refresh)

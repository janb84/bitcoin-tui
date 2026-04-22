btcui_set_name("Slow Blocks")

-- Constants
local TIP_DEPTH = 10000
local BLOCK_DISPLAY_DEPTH = 15
local BLOCK_TRACK_DEPTH = 100

-- Block tracking state
local blocks = {}             -- hash -> block info
local block_order = {}        -- array of hashes in arrival order
local pending_connect_secs = nil

-- Cached chain tips (fetched by tips timer, used by display timer)
local cached_chaintips = {}
local cached_tips = {}        -- hash -> { letter, height, status }
local cached_active_height = 0

local function get_or_create_block(ts, hash)
    local b = blocks[hash]
    if not b then
        b = {
            height = nil,
            hash = hash,
            time_header = ts,
            time_block = nil,
            compact = nil,
            txns_requested = nil,
            validation_secs = nil,
            size = nil,
            tx_count = nil,
        }
        blocks[hash] = b
        table.insert(block_order, hash)
    end
    return b
end

----------------------------------------------------------------------
-- Tables
----------------------------------------------------------------------

local block_table = btcui_table({
    key = "seq",
    title = "Recent Blocks (lua)",
    columns = {
        { name = "height", header = "Height", type = "number" },
        { name = "code", header = " " },
        { name = "hash", header = "Hash" },
        { name = "header", header = "Header", type = "timestamp" },
        { name = "compact", header = "Compact" },
        { name = "block", header = "Block\nDelay (s)", type = "number", decimals = 3 },
        { name = "validate", header = "Validation\nDelay (s)", type = "number", decimals = 3 },
        { name = "size", header = "Size (kB)", type = "number", decimals = 1 },
        { name = "txs", header = "TXs", type = "number" },
    },
})

local tip_table = btcui_table({
    key = "seq",
    title = "Recent Chain Tips",
    no_header = true,
    columns = {
        { name = "code", header = "*" },
        { name = "status", header = "Status" },
        { name = "height", header = "Height" },
        { name = "hash", header = "Hash" },
    },
})

local log_table = btcui_table({
    key = "seq",
    title = "Log Watcher",
    columns = {
        { name = "timestamp", header = "Time", type = "timestamp" },
        { name = "msg", header = "Message" },
    },
})

----------------------------------------------------------------------
-- Display helpers
----------------------------------------------------------------------

local function embolden(v)
    if v == nil then return nil end
    if type(v) == "table" then
        v.bold = true
        return v
    else
        return { value = v, bold = true }
    end
end

local function gray_all_if(cond, tbl)
    if cond then
        for k, v in pairs(tbl) do
            if type(v) == "table" then
                v.color = "gray"
            elseif v ~= nil then
                tbl[k] = { value = v, color = "gray" }
            end
        end
    end
    return tbl
end

local function num_color(n, max_green, max_yellow)
    if n == nil then return nil end
    if n < max_green then
        return { value = n, color = "green" }
    elseif n < max_yellow then
        return { value = n, color = "yellow" }
    else
        return { value = n, color = "red" }
    end
end

local function code_color(code)
    if code ~= nil and code:sub(1, 1) == "A" then return { value = code, color = "cyan" } end
    return code
end

local function abbrev_hash(h)
    if h == nil then return h end
    return h:sub(1, 8) .. "..." .. h:sub(-12)
end

----------------------------------------------------------------------
-- Chain tips timer: fetch via RPC, update cached state + tip table
----------------------------------------------------------------------

-- Build tip code strings for each block hash (e.g. "A", "Ab", " b")
local function tip_codes(chaintips, tips)
    local codes = {}
    local prefix = ""
    for _, tip in ipairs(chaintips) do
        local info = tips[tip.hash]
        if info then
            local cur = tip.hash
            for _ = 1, 100 do
                if not cur or not blocks[cur] then break end
                if not codes[cur] then codes[cur] = prefix end
                codes[cur] = codes[cur] .. info.letter
                cur = blocks[cur].prev
            end
            prefix = prefix .. " "
        end
    end
    return codes
end

local tips_timer = btcui_set_interval(30, function()
    local chaintips = btcui_rpc("getchaintips")
    if not chaintips then return end

    local tips = {}
    local active_hash = nil
    local active_height = 0
    for _, tip in ipairs(chaintips) do
        if tip.status == "active" then
            active_hash = tip.hash
            active_height = tip.height
        end
    end

    local row = 1
    tip_table:start_refresh()
    for _, tip in ipairs(chaintips) do
        if active_hash and tip.height >= active_height - TIP_DEPTH then
            local letter
            if tip.hash == active_hash then
                letter = "A"
                tip_table:update(1, {
                    height = "height=" .. tostring(tip.height),
                    code = { value = "A", color = "cyan" },
                    status = { value = tip.status, color = "green" },
                    hash = { value = tip.hash, color = "gray" },
                })
            else
                row = row + 1
                letter = string.char(math.min(row, 26) + 96)
                local status = tip.status
                if status == "valid-fork" or status == "valid-headers" then
                    status = { value = status, color = "yellow" }
                end
                tip_table:update(row, {
                    height = "height=" .. tostring(tip.height),
                    code = letter,
                    status = status,
                    hash = { value = tip.hash, color = "gray" },
                })
            end
            tips[tip.hash] = { letter = letter, height = tip.height, status = tip.status }
        end
    end
    tip_table:finish_refresh()

    cached_chaintips = chaintips
    cached_tips = tips
    cached_active_height = active_height
end)

----------------------------------------------------------------------
-- Enrich timer: fill in missing block data via RPC
----------------------------------------------------------------------

btcui_set_interval(1, function()
    -- Prune blocks well below display depth
    if cached_active_height > 0 and #block_order > BLOCK_TRACK_DEPTH then
        local cutoff = cached_active_height - BLOCK_TRACK_DEPTH
        local new_order = {}
        for _, hash in ipairs(block_order) do
            local b = blocks[hash]
            if b and b.height and b.height < cutoff then
                blocks[hash] = nil
            else
                table.insert(new_order, hash)
            end
        end
        block_order = new_order
    end

    -- Enrich blocks missing data
    for _, hash in ipairs(block_order) do
        local b = blocks[hash]
        if b and not b.prev then
            local hdr = btcui_rpc("getblockheader", hash)
            if hdr then b.prev = hdr.previousblockhash end
        end
        if b and not b.size then
            local blk = btcui_rpc("getblock", hash, 1)
            if blk and blk.size then b.size = blk.size / 1000; b.tx_count = blk.nTx end
        end
    end
end)

----------------------------------------------------------------------
-- Display timer: refresh block table from cached data (no RPCs)
----------------------------------------------------------------------

btcui_set_interval(1, function()
    if cached_active_height == 0 then return end
    local codes = tip_codes(cached_chaintips, cached_tips)

    block_table:start_refresh()
    for idx, hash in ipairs(block_order) do
        local b = blocks[hash]
        if b and b.height and b.height >= cached_active_height - BLOCK_DISPLAY_DEPTH then
            local delta = nil
            if b.time_block and b.time_header then delta = b.time_block - b.time_header end
            local compact = ""
            if b.time_block then
                if b.compact then
                    if b.txns_requested == nil then
                        compact = { value = "yes (header)", color = "yellow" }
                    elseif b.txns_requested > 0 then
                        compact = { value = "yes (" .. tostring(b.txns_requested) .. " req)", color = "yellow" }
                    else
                        compact = { value = "yes", color = "green" }
                    end
                else
                    compact = { value = "no", color = "gray" }
                end
            end
            local code = codes[hash]
            local inactive = (code == nil or code:sub(1, 1) ~= "A")
            block_table:update(idx, gray_all_if(inactive, {
                height = b.height,
                code = code_color(code),
                hash = abbrev_hash(hash),
                header = b.time_header,
                compact = compact,
                block = num_color(delta, 1, 10),
                validate = embolden(num_color(b.validation_secs, 0.5, 5.0)),
                size = b.size,
                txs = b.tx_count,
            }))
        end
    end
    block_table:finish_refresh()
end)

----------------------------------------------------------------------
-- Block updates (log watchers)
----------------------------------------------------------------------

local BACKLOG = 2 * 1024 * 1024

btcui_watch_log("Saw new (cmpctblock )?header hash=(\\w+) height=(\\d+)", function(ts, msg, compact, hash, height)
    local b = get_or_create_block(ts, hash)
    b.height = tonumber(height)
    b.compact = (compact ~= "")
end, BACKLOG)

btcui_watch_log("Successfully reconstructed block (\\w+) with (\\d+) txn prefilled, (\\d+) txn from mempool \\(incl at least \\d+ from extra pool\\) and (\\d+) txn", function(ts, msg, hash, prefilled, from_mempool, requested)
    local b = get_or_create_block(ts, hash)
    if not b.time_block then
        b.time_block = ts
        b.compact = true
        b.txns_requested = tonumber(requested)
    end
end, BACKLOG)

btcui_watch_log("received block (\\w+) peer=", function(ts, msg, hash)
    local b = get_or_create_block(ts, hash)
    if not b.time_block then b.time_block = ts end
end, BACKLOG)

btcui_watch_log("- Connect block: ([0-9.]+)ms", function(ts, msg, elapsed)
    pending_connect_secs = tonumber(elapsed) / 1000.0
end, BACKLOG)

btcui_watch_log("UpdateTip: new best=(\\w+) height=(\\d+)", function(ts, msg, hash, height)
    local h = tonumber(height)
    local b = get_or_create_block(ts, hash)
    if not b.height then b.height = h end
    if not b.time_block then b.time_block = ts end
    if b.compact == nil then b.compact = false end
    if pending_connect_secs then
        b.validation_secs = pending_connect_secs
        pending_connect_secs = nil
    end
    btcui_wake(tips_timer)
end, BACKLOG)

----------------------------------------------------------------------
-- Footer buttons
----------------------------------------------------------------------

btcui_add_footer_button(" ↺ refresh tips", function()
    btcui_wake(tips_timer)
end)

----------------------------------------------------------------------
-- Log tailing
----------------------------------------------------------------------

local LOG_BACKLOG = 5000
local LOG_LINES = 10

local log_seq = 0
btcui_watch_log("^", function(ts, msg)
    log_seq = log_seq + 1
    log_table:set_header_info( { value = "[lines=" .. tostring(log_seq) .. "]", color = "gray" })
    log_table:update(log_seq, { timestamp = ts, msg = msg })
    local old = log_seq - LOG_LINES
    while log_table:remove(old) do old = old - 1 end
end, LOG_BACKLOG)

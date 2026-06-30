-- Tools — Lua port of the built-in C++ Tools tab (src/tabs/tools.cpp).
--
-- Built from the composable btcui_table component and the shared text-input
-- overlay:
--   • Broadcast — paste raw tx hex, sendrawtransaction, show the txid (Enter on it
--     runs the global tx search) or the node's error message.
--   • Private Broadcast Queue — txids returned by getprivatebroadcastinfo.
--   • Shutdown — stop bitcoind, then exit the TUI.
--
-- Explanatory rows (help, status, errors, descriptions) are marked
-- __selectable=false so navigation skips them — only the txid result and the
-- shutdown action can be focused/activated.
--
-- This tab is auto-injected by bitcoin-tui with a per-tab allow_rpc grant for the
-- three mutating RPCs (sendrawtransaction / stop / getprivatebroadcastinfo); the
-- global Lua sandbox stays read-only. btcui_rpc can only run inside the refresh
-- timer (it yields the coroutine), so button/select/input callbacks set a pending
-- flag and btcui_wake() the timer, which performs the RPC.

-- Lua 5.5 strict globals: a typo in any name below is caught at load time.
global btcui_add_footer_button, btcui_on_select, btcui_option, btcui_quit,
       btcui_rpc, btcui_search, btcui_set_interval, btcui_set_name, btcui_table,
       btcui_text_input, btcui_wake, ipairs, pcall, string, table, tonumber,
       tostring, type

btcui_set_name("Tools")

local REFRESH = tonumber(btcui_option("interval", "5")) or 5

----------------------------------------------------------------------
-- State
----------------------------------------------------------------------

local refresh_timer                 -- assigned by btcui_set_interval (used by btcui_wake)
local pending_broadcast = nil       -- hex awaiting sendrawtransaction
local pending_shutdown  = false     -- set when the user asks to stop & exit
-- bstatus: nil (idle) | {kind="submitting"}
--                     | {kind="ok",  txid=…}
--                     | {kind="err", error=…}
local bstatus = nil

----------------------------------------------------------------------
-- Helpers
----------------------------------------------------------------------

-- Word-wrap `msg` to `width` columns (mirrors the error wrapping in tools.cpp).
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

----------------------------------------------------------------------
-- Panels (created once at load; populated by update_display / refresh)
----------------------------------------------------------------------

-- A one-text-column table: key column hidden, header row suppressed. `selectable`
-- false marks a whole table display-only; individual rows can also opt out by
-- passing __selectable=false to :update().
local function text_table(title, selectable)
    return btcui_table({
        title      = title,
        no_header  = true,
        selectable = selectable,
        key        = "id",
        columns    = {
            { name = "id",   header = "" },   -- hidden key column
            { name = "text", header = "·" },  -- visible (header hidden by no_header)
        },
    })
end

local broadcast_panel = text_table("Broadcast Transaction", true)
local queue_panel     = text_table("Private Broadcast Queue", false) -- display-only
local shutdown_panel  = text_table("Shutdown", true)

-- A display-only (non-selectable) row helper.
local function info_row(panel, id, value, color)
    panel:update(id, {
        id = id, text = { value = value, color = color or "gray" }, __selectable = false,
    })
end

-- Shutdown rows: one selectable action row, one display-only description.
local function refresh_shutdown()
    shutdown_panel:start_refresh()
    shutdown_panel:update("shutdown", {
        id = "shutdown", text = { value = "Shutdown bitcoind & exit", color = "white" },
    })
    info_row(shutdown_panel, "info", "Sends RPC stop to Bitcoin Core, then exits the TUI.")
    shutdown_panel:finish_refresh()
end

----------------------------------------------------------------------
-- Rendering
----------------------------------------------------------------------

-- Rows are rendered in key order; the keys below are chosen so the selectable
-- "broadcast" action row stays on top, then help / status / result / error.
local function update_display()
    broadcast_panel:start_refresh()
    -- Selectable action row (Enter opens the same dialog as [b]).
    broadcast_panel:update("broadcast", {
        id = "broadcast", text = { value = "Broadcast a transaction", color = "white" },
    })
    if bstatus == nil then
        info_row(broadcast_panel, "help1", "Paste a raw transaction")
        info_row(broadcast_panel, "help2", "connected peers over the P2P network.")
    elseif bstatus.kind == "submitting" then
        info_row(broadcast_panel, "status", "Broadcasting…", "yellow")
    elseif bstatus.kind == "ok" then
        -- Fixed "result" key (selectable): Enter looks up the broadcast txid.
        broadcast_panel:update("result", {
            id = "result", text = { value = "✓ " .. bstatus.txid, color = "green", bold = true },
        })
        info_row(broadcast_panel, "resultz", "Enter on the txid to look it up.")
    elseif bstatus.kind == "err" then
        info_row(broadcast_panel, "err0", "Error:", "red")
        for i, line in ipairs(wrap(bstatus.error, 72)) do
            broadcast_panel:update("err" .. i, {
                id = "err" .. i, text = { value = line, color = "white" }, __selectable = false,
            })
        end
    end
    broadcast_panel:finish_refresh()
end

----------------------------------------------------------------------
-- Actions
----------------------------------------------------------------------

local function open_broadcast()
    btcui_text_input("Paste raw transaction hex:", "", function(hex)
        if hex == nil then return end          -- cancelled
        hex = hex:gsub("%s+", "")              -- strip all whitespace
        if hex == "" then return end           -- empty submit is a no-op
        pending_broadcast = hex
        bstatus           = { kind = "submitting" }
        update_display()
        btcui_wake(refresh_timer)              -- perform the RPC on the next timer tick
    end)
end

local function do_shutdown()
    pending_shutdown = true
    btcui_wake(refresh_timer)
end

-- Enter/click activates a selectable row: the Broadcast / Shutdown action rows, or
-- the broadcast-result txid (queue txids are display-only, matching the C++ tab).
btcui_on_select(function(key)
    if key == "broadcast" then
        open_broadcast()
    elseif key == "shutdown" then
        do_shutdown()
    elseif key == "result" and bstatus and bstatus.kind == "ok" then
        btcui_search(bstatus.txid)
    end
end)


----------------------------------------------------------------------
-- Refresh — the only place btcui_rpc may run (it yields the coroutine)
----------------------------------------------------------------------

local function refresh()
    -- Shutdown: stop the node (ignore the inevitable connection-drop error) and exit.
    if pending_shutdown then
        pcall(btcui_rpc, "stop")
        btcui_quit()
        return
    end

    -- Broadcast a pending transaction.
    if pending_broadcast then
        local hex = pending_broadcast
        pending_broadcast = nil
        local ok, res = pcall(btcui_rpc, "sendrawtransaction", hex)
        if ok then
            bstatus = { kind = "ok", txid = tostring(res) }
        else
            bstatus = { kind = "err", error = tostring(res) }
        end
    end

    -- Private broadcast queue (Bitcoin Core PR #29415 — absent on older nodes).
    queue_panel:start_refresh()
    local ok, pb = pcall(btcui_rpc, "getprivatebroadcastinfo")
    local count = 0
    if ok and type(pb) == "table" then
        for _, entry in ipairs(pb) do
            local txid = (type(entry) == "table") and entry.txid or entry
            if type(txid) == "string" then
                count = count + 1
                queue_panel:update(txid, { id = txid, text = { value = txid, color = "white" } })
            end
        end
    end
    queue_panel:finish_refresh()
    queue_panel:set_header_info(count == 0 and { value = "(empty)", color = "gray" } or "")

    update_display()
end

update_display()
refresh_shutdown()
refresh_timer = btcui_set_interval(REFRESH, refresh)

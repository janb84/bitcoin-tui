-- Peers — Lua port of the built-in C++ Peers tab (src/tabs/peers.cpp).
--
-- Built from the composable btcui_table component and the btcui_dialog modal
-- overlay:
--   • Peers list      — getpeerinfo, one row per peer (Enter opens the detail).
--   • Peer detail     — full peer info with Disconnect / Ban (24h) actions.
--   • Added Nodes     — getaddednodeinfo list; Enter removes, [a] adds a node.
--   • Ban List        — listbanned with remaining ban time; Enter unbans,
--                       [b] opens the ban/unban dialog for an arbitrary address.
--
-- This tab is auto-injected by bitcoin-tui with a per-tab allow_rpc grant for
-- the three mutating RPCs (addnode / disconnectnode / setban); the global Lua
-- sandbox stays read-only. btcui_rpc can only run inside the refresh timer (it
-- yields the coroutine), so dialog callbacks set a pending flag and
-- btcui_wake() the timer, which performs the RPC.

-- Lua 5.5 strict globals: a typo in any name below is caught at load time.
global btcui_add_footer_button, btcui_dialog, btcui_now, btcui_on_select,
       btcui_option, btcui_quit, btcui_rpc, btcui_set_interval, btcui_set_name,
       btcui_table, btcui_wake, ipairs, math, pcall, string, table, tonumber,
       tostring, type

btcui_set_name("Peers")

local REFRESH = tonumber(btcui_option("interval", "5")) or 5

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
    return string.format("%d B", b)
end

local function fmt_age(secs)
    if secs < 60 then return secs .. "s" end
    if secs < 3600 then return math.floor(secs / 60) .. "m " .. (secs % 60) .. "s" end
    if secs < 86400 then
        return math.floor(secs / 3600) .. "h " .. math.floor((secs % 3600) / 60) .. "m"
    end
    -- Roll over to days, so a long-lived peer's uptime is readable.
    return math.floor(secs / 86400) .. "d " .. math.floor((secs % 86400) / 3600) .. "h"
end

-- Display width in codepoints (labels contain multi-byte glyphs like "—").
local function dlen(s)
    local n = 0
    for i = 1, #s do
        local b = s:byte(i)
        if b < 0x80 or b >= 0xC0 then n = n + 1 end
    end
    return n
end

-- Left-pad to `w` columns: poor man's right alignment for string cells.
local function lpad(s, w) return string.rep(" ", math.max(0, w - dlen(s))) .. s end

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

-- IPv6 starts with '['; an IPv4 host is all digits+dots before the port colon.
local function is_ip_addr(addr)
    if addr:sub(1, 1) == "[" then return true end
    local host = addr:match("^([^:]*)")
    return host ~= nil and host ~= "" and host:match("^[%d%.]+$") ~= nil
end

-- Strip the port for setban: "[v6]:port" -> "[v6]", "v4:port" -> "v4".
local function strip_port(addr)
    if addr:sub(1, 1) == "[" then
        local e = addr:find("]", 1, true)
        return e and addr:sub(1, e) or addr
    end
    local host = addr:match("^(.*):")  -- up to the last colon
    return host or addr
end

----------------------------------------------------------------------
-- State
----------------------------------------------------------------------

local refresh_timer            -- assigned by btcui_set_interval (used by btcui_wake)
local view = "list"            -- list | detail | action | addednodes | banlist
                               -- | addnode | baninput
local peers_by_id = {}         -- key (tostring(id)) -> normalized peer info

-- Pending mutations performed by the refresh timer (btcui_rpc yields, so it
-- cannot run inside dialog callbacks).
local pending_action  = nil    -- { addr=…, is_ban=… }  disconnect / ban a peer
local pending_addnode = nil    -- { addr=…, cmd=… }     addnode onetry/add
local pending_setban  = nil    -- { addr=…, remove=… }  ban/unban an address
local pending_remove  = nil    -- addr                  remove an added node
local pending_unban   = nil    -- addr                  unban from the ban list

-- Lazily fetched overlay data, invalidated by the mutations above.
local added_nodes,  added_loaded  = {}, false
local banned_list,  banned_loaded = {}, false

local CMDS = { "onetry", "add" }
local addnode_cmd = 1          -- remembered across Add Node dialogs (like C++)

----------------------------------------------------------------------
-- Peers list
----------------------------------------------------------------------

local peers_table = btcui_table({
    key     = "id",
    columns = {
        { name = "id",     header = "ID", type = "number" },
        { name = "addr",   header = "Address" },
        { name = "net",    header = "Net" },
        { name = "io",     header = "I/O" },
        { name = "ping",   header = "  Ping ms" },
        { name = "recv",   header = "     Recv" },
        { name = "sent",   header = "     Sent" },
        { name = "height", header = "   Height" },
    },
})

-- Normalize one getpeerinfo entry into the fields the tab uses.
local function normalize(p)
    local services = ""
    if type(p.servicesnames) == "table" then
        services = table.concat(p.servicesnames, ", ")
    end
    return {
        id              = p.id or 0,
        addr            = p.addr or "",
        network         = p.network or "",
        subver          = p.subver or "",
        inbound         = p.inbound == true,
        bytes_sent      = p.bytessent or 0,
        bytes_recv      = p.bytesrecv or 0,
        version         = p.version or 0,
        synced_blocks   = p.synced_blocks or 0,
        conntime        = p.conntime or 0,
        connection_type = p.connection_type or "",
        transport       = p.transport_protocol_type or "",
        addr_processed  = p.addr_processed or 0,
        services        = services,
        ping_ms         = type(p.pingtime) == "number" and p.pingtime * 1000 or nil,
        min_ping_ms     = type(p.minping) == "number" and p.minping * 1000 or nil,
        hb_from         = p.bip152_hb_from == true,
        hb_to           = p.bip152_hb_to == true,
    }
end

local function update_peers_table(pi)
    peers_table:start_refresh()
    peers_by_id = {}
    for _, raw in ipairs(pi) do
        local p = normalize(raw)
        peers_by_id[tostring(p.id)] = p
        peers_table:update(p.id, {
            id     = p.id,
            addr   = p.addr,
            net    = p.network ~= "" and p.network:sub(1, 4) or "?",
            io     = { value = p.inbound and "in" or "out",
                       color = p.inbound and "cyan" or "green" },
            ping   = lpad(p.ping_ms and string.format("%.1f", p.ping_ms) or "—", 9),
            recv   = lpad(fmt_bytes(p.bytes_recv), 9),
            sent   = lpad(fmt_bytes(p.bytes_sent), 9),
            height = lpad(fmt_height(p.synced_blocks), 9),
        })
    end
    peers_table:finish_refresh()
end

----------------------------------------------------------------------
-- Dialogs — each show_* function (re)opens the modal for its view
----------------------------------------------------------------------

local show_added_nodes, show_ban_list  -- forward declarations

-- Working / result overlay for a disconnect/ban action.
local function show_action_working()
    view = "action"
    btcui_dialog({
        title    = "Peer Action",
        width    = 40,
        rows     = { { text = "  Working…", color = "yellow", bold = true } },
        closable = false,
        on_event = function(ev)
            if ev.type == "key" and ev.key == "q" then btcui_quit() end
        end,
    })
end

local function show_action_result(ok, msg)
    view = "action"
    local rows = {}
    if ok then
        rows[1] = { text = "  ✓ " .. msg, color = "green", bold = true }
    else
        rows[1] = { text = "  ✗ Error:", color = "red" }
        for _, line in ipairs(wrap(msg, 60)) do
            rows[#rows + 1] = { text = "  " .. line }
        end
    end
    btcui_dialog({
        title    = "Peer Action",
        width    = 68,
        rows     = rows,
        hint     = "[Esc] to dismiss",
        on_event = function(ev)
            if ev.type == "close" then
                view = "list"
            elseif ev.type == "key" and ev.key == "q" then
                btcui_quit()
            end
        end,
    })
end

-- Peer detail overlay with Disconnect / Ban buttons.
local function show_detail(p)
    view = "detail"

    local function lv(label, value, color)
        return { label = string.format("  %-12s: ", label), value = value, color = color }
    end
    local function dash(v) return (v ~= nil and v ~= "") and v or "—" end
    local function ping(v) return v and string.format("%.1f ms", v) or "—" end

    local hb
    if p.hb_from and p.hb_to then hb = "both"
    elseif p.hb_from then hb = "from"
    elseif p.hb_to then hb = "to"
    else hb = "no" end

    local rows = {}
    if is_ip_addr(p.addr) then
        rows[#rows + 1] = lv("Address", p.addr)
    else
        -- Long overlay-network addresses get their own indented line.
        rows[#rows + 1] = { text = "  Address     : ", color = "gray" }
        rows[#rows + 1] = { text = "    " .. p.addr, bold = true }
    end
    rows[#rows + 1] = lv("Direction", p.inbound and "inbound" or "outbound",
                         p.inbound and "cyan" or "green")
    rows[#rows + 1] = lv("Network", dash(p.network ~= "" and p.network or "?"))
    rows[#rows + 1] = lv("User agent", p.subver)
    rows[#rows + 1] = lv("Version", tostring(p.version))
    rows[#rows + 1] = lv("Services", dash(p.services))
    rows[#rows + 1] = "---"
    rows[#rows + 1] = lv("Ping", ping(p.ping_ms))
    rows[#rows + 1] = lv("Min ping", ping(p.min_ping_ms))
    rows[#rows + 1] = lv("Connected",
                         p.conntime > 0 and fmt_age(btcui_now() - p.conntime) or "—")
    rows[#rows + 1] = lv("Conn type", dash(p.connection_type))
    rows[#rows + 1] = lv("Transport", dash(p.transport))
    rows[#rows + 1] = "---"
    rows[#rows + 1] = lv("Recv", fmt_bytes(p.bytes_recv))
    rows[#rows + 1] = lv("Sent", fmt_bytes(p.bytes_sent))
    rows[#rows + 1] = lv("Height", fmt_height(p.synced_blocks))
    rows[#rows + 1] = lv("HB compact", hb)
    rows[#rows + 1] = lv("Addrs proc", fmt_int(p.addr_processed))

    local function start_action(is_ban)
        pending_action = { addr = p.addr, is_ban = is_ban }
        show_action_working()
        btcui_wake(refresh_timer)
    end

    btcui_dialog({
        title    = "Peer " .. p.id,
        width    = 78,
        rows     = rows,
        buttons  = { "Disconnect", "Ban (24h)" },
        hint     = "[←/→] select  [⏎] confirm  [Esc] back",
        on_event = function(ev)
            if ev.type == "button" then
                start_action(ev.index == 2)
            elseif ev.type == "key" then
                if ev.key == "d" then start_action(false)
                elseif ev.key == "b" then start_action(true)
                elseif ev.key == "q" then btcui_quit() end
            elseif ev.type == "close" then
                view = "list"
            end
        end,
    })
end

-- Add Node dialog (command toggle + address input), reached from Added Nodes.
local function show_addnode_progress(addr, ok, msg)
    -- ok == nil: still connecting; true/false: result
    local rows = {
        { label = "  Command : ", value = CMDS[addnode_cmd], color = "yellow" },
        { label = "  Address : ", value = addr },
        "---",
    }
    if ok == nil then
        rows[#rows + 1] = { text = "  Connecting…", color = "yellow" }
    elseif ok then
        rows[#rows + 1] = { text = "  ✓ " .. msg, color = "green", bold = true }
    else
        rows[#rows + 1] = { text = "  ✗ Error:", color = "red" }
        for _, line in ipairs(wrap(msg, 56)) do
            rows[#rows + 1] = { text = "  " .. line }
        end
    end
    btcui_dialog({
        title    = "Add Node",
        width    = 64,
        rows     = rows,
        hint     = "[Esc] close",
        on_event = function(ev)
            if ev.type == "close" then
                show_added_nodes()
            elseif ev.type == "key" and ev.key == "q" then
                btcui_quit()
            end
        end,
    })
end

local function show_addnode_input()
    view = "addnode"
    btcui_dialog({
        title    = "Add Node",
        width    = 64,
        choice   = { label = "Command", options = CMDS, index = addnode_cmd },
        input    = { label = "Address", value = "" },
        hint     = "[⏎] submit  [←/→] change  [Esc] cancel",
        on_event = function(ev)
            if ev.type == "submit" then
                addnode_cmd = ev.choice or addnode_cmd
                local addr = trim(ev.text or "")
                if addr == "" then
                    show_addnode_input()   -- nothing typed: stay in the dialog
                    return
                end
                pending_addnode = { addr = addr, cmd = CMDS[addnode_cmd] }
                show_addnode_progress(addr, nil)
                btcui_wake(refresh_timer)
            elseif ev.type == "close" then
                show_added_nodes()
            elseif ev.type == "key" and ev.key == "q" then
                btcui_quit()
            end
        end,
    })
end

-- Ban / Unban dialog (command toggle + address input), reached from Ban List.
local function show_ban_progress(addr, remove, ok, msg)
    local rows = {
        { label = "  Command : ", value = remove and "unban" or "ban", color = "yellow" },
        { label = "  Address : ", value = addr },
        "---",
    }
    if ok == nil then
        rows[#rows + 1] = { text = "  Submitting…", color = "yellow" }
    elseif ok then
        rows[#rows + 1] = { text = "  ✓ " .. msg, color = "green", bold = true }
    else
        rows[#rows + 1] = { text = "  ✗ Error:", color = "red" }
        for _, line in ipairs(wrap(msg, 56)) do
            rows[#rows + 1] = { text = "  " .. line }
        end
    end
    btcui_dialog({
        title    = "Ban / Unban Node",
        width    = 64,
        rows     = rows,
        hint     = "[Esc] close",
        on_event = function(ev)
            if ev.type == "close" then
                show_ban_list()
            elseif ev.type == "key" and ev.key == "q" then
                btcui_quit()
            end
        end,
    })
end

local function show_ban_input()
    view = "baninput"
    btcui_dialog({
        title    = "Ban / Unban Node",
        width    = 64,
        choice   = { label = "Command", options = { "ban", "unban" }, index = 1 },
        input    = { label = "Address", value = "" },
        hint     = "[⏎] submit  [←/→] toggle  [Esc] cancel",
        on_event = function(ev)
            if ev.type == "submit" then
                local addr = trim(ev.text or "")
                if addr == "" then
                    show_ban_input()
                    return
                end
                local remove = (ev.choice == 2)
                pending_setban = { addr = addr, remove = remove }
                show_ban_progress(addr, remove, nil)
                btcui_wake(refresh_timer)
            elseif ev.type == "close" then
                show_ban_list()
            elseif ev.type == "key" and ev.key == "q" then
                btcui_quit()
            end
        end,
    })
end

-- Added Nodes overlay: ●/○ connection dot per node, Enter removes.
show_added_nodes = function()
    view = "addednodes"
    local rows = {}
    if not added_loaded then
        rows[1] = { text = "  Loading…", color = "gray" }
    elseif #added_nodes == 0 then
        rows[1] = { text = "  (none)", color = "gray" }
    else
        for _, n in ipairs(added_nodes) do
            rows[#rows + 1] = {
                key   = n.addr,
                spans = {
                    { text = n.connected and " ● " or " ○ ",
                      color = n.connected and "green" or "gray" },
                    { text = n.addr },
                },
            }
        end
    end
    btcui_dialog({
        title    = "Added Nodes",
        width    = 64,
        rows     = rows,
        hint     = "[↑/↓] navigate  [⏎] remove  [a] add node  [Esc] close",
        on_event = function(ev)
            if ev.type == "select" then
                pending_remove = ev.key
                btcui_wake(refresh_timer)
            elseif ev.type == "key" then
                if ev.key == "a" then show_addnode_input()
                elseif ev.key == "q" then btcui_quit() end
            elseif ev.type == "close" then
                view = "list"
            end
        end,
    })
end

-- Ban List overlay: banned address + remaining ban time, Enter unbans.
show_ban_list = function()
    view = "banlist"
    local now = btcui_now()
    local rows = {}
    if not banned_loaded then
        rows[1] = { text = "  Loading…", color = "gray" }
    elseif #banned_list == 0 then
        rows[1] = { text = "  (none)", color = "gray" }
    else
        for _, e in ipairs(banned_list) do
            local remaining = (e.banned_until or 0) - now
            rows[#rows + 1] = {
                key         = e.address,
                text        = " " .. e.address,
                right       = remaining > 0 and fmt_age(remaining) or "expired",
                right_color = remaining > 0 and "" or "gray",
            }
        end
    end
    btcui_dialog({
        title    = "Ban List",
        width    = 64,
        rows     = rows,
        hint     = "[↑/↓] navigate  [⏎] unban  [b] ban∕unban  [Esc] close",
        on_event = function(ev)
            if ev.type == "select" then
                pending_unban = ev.key
                btcui_wake(refresh_timer)
            elseif ev.type == "key" then
                if ev.key == "b" then show_ban_input()
                elseif ev.key == "q" then btcui_quit() end
            elseif ev.type == "close" then
                view = "list"
            end
        end,
    })
end

local function open_added_nodes()
    show_added_nodes()
    if not added_loaded then btcui_wake(refresh_timer) end
end

local function open_ban_list()
    show_ban_list()
    if not banned_loaded then btcui_wake(refresh_timer) end
end

----------------------------------------------------------------------
-- Refresh — the only place btcui_rpc may run (it yields the coroutine)
----------------------------------------------------------------------

local function refresh()
    -- 1. Pending mutations queued by dialog callbacks.
    if pending_action then
        local a = pending_action
        pending_action = nil
        local ok, res, msg
        if a.is_ban then
            local ip = strip_port(a.addr)
            ok, res = pcall(btcui_rpc, "setban", ip, "add")
            msg = "Banned " .. ip
            banned_loaded = false
        else
            ok, res = pcall(btcui_rpc, "disconnectnode", a.addr)
            msg = "Disconnected"
        end
        if view == "action" then
            show_action_result(ok, ok and msg or tostring(res))
        end
    end

    if pending_addnode then
        local a = pending_addnode
        pending_addnode = nil
        local ok, res = pcall(btcui_rpc, "addnode", a.addr, a.cmd)
        added_loaded = false
        if view == "addnode" then
            show_addnode_progress(a.addr, ok, ok and (a.cmd .. " " .. a.addr) or tostring(res))
        end
    end

    if pending_setban then
        local a = pending_setban
        pending_setban = nil
        local ok, res = pcall(btcui_rpc, "setban", a.addr, a.remove and "remove" or "add")
        banned_loaded = false
        if view == "baninput" then
            show_ban_progress(a.addr, a.remove, ok,
                              ok and ((a.remove and "Unbanned " or "Banned ") .. a.addr)
                                 or tostring(res))
        end
    end

    if pending_remove then
        local addr = pending_remove
        pending_remove = nil
        pcall(btcui_rpc, "addnode", addr, "remove")
        added_loaded = false
    end

    if pending_unban then
        local addr = pending_unban
        pending_unban = nil
        pcall(btcui_rpc, "setban", addr, "remove")
        banned_loaded = false
    end

    -- 2. Lazily (re)fetch overlay data while its dialog is open.
    if view == "addednodes" and not added_loaded then
        added_nodes = {}
        local ok, ans = pcall(btcui_rpc, "getaddednodeinfo")
        if ok and type(ans) == "table" then
            for _, n in ipairs(ans) do
                local connected = false
                if type(n.addresses) == "table" then
                    for _, a in ipairs(n.addresses) do
                        if a.connected then connected = true break end
                    end
                end
                added_nodes[#added_nodes + 1] = { addr = n.addednode or "",
                                                  connected = connected }
            end
        end
        added_loaded = true
        if view == "addednodes" then show_added_nodes() end
    end

    if view == "banlist" and not banned_loaded then
        banned_list = {}
        local ok, bl = pcall(btcui_rpc, "listbanned")
        if ok and type(bl) == "table" then
            for _, b in ipairs(bl) do
                banned_list[#banned_list + 1] = { address      = b.address or "",
                                                  banned_until = b.banned_until or 0 }
            end
        end
        banned_loaded = true
        if view == "banlist" then show_ban_list() end
    end

    -- 3. Peers list.
    local ok, pi = pcall(btcui_rpc, "getpeerinfo")
    if ok and type(pi) == "table" then
        update_peers_table(pi)
    end
end

----------------------------------------------------------------------
-- Interaction
----------------------------------------------------------------------

-- Enter/click on a peer row opens its detail overlay.
btcui_on_select(function(key)
    local p = peers_by_id[key]
    if p then show_detail(p) end
end)

btcui_add_footer_button("[a] add node", open_added_nodes)
btcui_add_footer_button("[b] ban list", open_ban_list)

refresh_timer = btcui_set_interval(REFRESH, refresh)

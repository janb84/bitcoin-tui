-- Feerate diagram — visualizes the cluster-mempool feerate diagram for
-- the entire mempool. The whole curve is rendered as an area plot
-- (cumulative weight on X, cumulative fee on Y) using Unicode half-block
-- characters, giving an at-a-glance view of how feerate tapers off as
-- more of the mempool is stacked into a block template.
--
-- Requires Bitcoin Core v31+ (for the getmempoolfeeratediagram RPC).

btcui_set_name("Feerate")

local REFRESH     = tonumber(btcui_option("refresh", 5))
local PLOT_WIDTH  = tonumber(btcui_option("width", 80))
local PLOT_HEIGHT = tonumber(btcui_option("height", 14))
local BLOCK_WU    = 4000000  -- 4 MWU = one block

----------------------------------------------------------------------
-- Panels
----------------------------------------------------------------------

local mempool_summary = btcui_summary({
    title = "Mempool",
    fields = {
        { name = "txcount",    label = "TXs",                  type = "number" },
        { name = "vsize",      label = "VSize (vMB)",          type = "number", decimals = 2 },
        { name = "total_fees", label = "Total fees (BTC)",     type = "number", decimals = 4 },
        { name = "minrelay",   label = "Min relay (sat/vB)",   type = "number", decimals = 3 },
        { name = "mempoolmin", label = "Mempool min (sat/vB)", type = "number", decimals = 3 },
    },
})

local block_summary = btcui_summary({
    title = "Next block (≤ 4 MWU)",
    fields = {
        { name = "top_feerate",  label = "Top feerate (sat/vB)",  type = "number", decimals = 2 },
        { name = "tail_feerate", label = "Tail feerate (sat/vB)", type = "number", decimals = 2 },
        { name = "chunk_count",  label = "Chunks",                type = "number" },
        { name = "weight_mwu",   label = "Weight (MWU)",          type = "number", decimals = 3 },
        { name = "fees",         label = "Fees (BTC)",            type = "number", decimals = 5 },
        { name = "fill_pct",     label = "Fill (%)",              type = "number", decimals = 1 },
    },
})

-- Single-column table: each row is a pre-composed line (Y-label + border
-- + plot / axis line / X-labels). A single column avoids the per-column
-- gutter that would otherwise put a 1-char gap between the border and
-- the leftmost plot cell. Rows 1..PLOT_HEIGHT are the plot; row
-- PLOT_HEIGHT+1 is the axis line; row PLOT_HEIGHT+2 is the X-axis labels.
local diagram_table = btcui_table({
    key = "row",
    title = "Feerate diagram",
    no_header = true,
    columns = {
        { name = "line", header = " " },
    },
})

local AXIS_PAD = 8   -- chars reserved for Y labels + border on the left

----------------------------------------------------------------------
-- Helpers
----------------------------------------------------------------------

-- Produces a string of at most 6 display characters, left-padded with
-- spaces. Truncates rather than overflowing so callers can rely on
-- the width for column alignment.
local function fmt_btc(btc)
    if btc == nil or btc <= 0 then return "     0" end
    local s
    if     btc >= 1000 then s = string.format("%.0f", btc)
    elseif btc >= 10   then s = string.format("%.2f", btc)
    elseif btc >= 0.1  then s = string.format("%.4f", btc)
    else                    s = string.format("%.5f", btc)
    end
    if #s > 6 then s = s:sub(1, 6) end
    return string.rep(" ", 6 - #s) .. s
end

local function fmt_mwu(wu)
    if wu >= 1e6 then return string.format("%.2f MWU", wu / 1e6) end
    if wu >= 1e3 then return string.format("%.0f kWU", wu / 1e3) end
    return string.format("%d WU", math.floor(wu))
end

local function sat_per_vb(wu, btc)
    if wu <= 0 then return 0 end
    return (btc * 1e8) * 4 / wu
end

local function feerate_color(f)
    if f == nil then return nil end
    if     f >= 100 then return "green"
    elseif f >= 10  then return "cyan"
    elseif f >= 1   then return "yellow"
    else                 return "gray" end
end

-- Render an area plot of the cumulative curve `pts` over the box
-- [0, x_extent] × [0, y_extent]. `block_col` is the column index of the
-- 4 MWU cutoff (or nil to omit). Returns an array of PLOT_HEIGHT strings
-- (top → bottom), each PLOT_WIDTH display cells wide.
local function render_plot(pts, x_extent, y_extent, block_col)
    local lines = {}
    if x_extent <= 0 or y_extent <= 0 or #pts < 2 then
        for _ = 1, PLOT_HEIGHT do table.insert(lines, string.rep(" ", PLOT_WIDTH)) end
        return lines
    end

    -- Bar height per column, in half-row units (0 .. 2*PLOT_HEIGHT).
    local bars = {}
    local di = 2  -- interpolating between pts[di-1] and pts[di]
    for x = 1, PLOT_WIDTH do
        local wt = (x - 0.5) / PLOT_WIDTH * x_extent
        while di <= #pts and pts[di].weight < wt do di = di + 1 end
        if di > #pts then di = #pts end
        local w0, f0 = pts[di-1].weight, pts[di-1].fee
        local w1, f1 = pts[di].weight,   pts[di].fee
        local f
        if w1 <= w0 then f = f1
        else             f = f0 + (f1 - f0) * (wt - w0) / (w1 - w0) end
        if f < 0         then f = 0 end
        if f > y_extent  then f = y_extent end
        bars[x] = (f / y_extent) * PLOT_HEIGHT * 2
    end

    for y = 0, PLOT_HEIGHT - 1 do
        -- Display row y covers half-rows [2(H-y-1), 2(H-y-1)+1].
        local th_full = 2 * (PLOT_HEIGHT - y)
        local th_half = 2 * (PLOT_HEIGHT - y) - 1
        local buf = {}
        for x = 1, PLOT_WIDTH do
            local b = bars[x]
            if b >= th_full then
                buf[x] = "█"
            elseif b >= th_half then
                buf[x] = "▄"
            elseif block_col and x == block_col then
                buf[x] = "┊"  -- dashed vertical marker for 4 MWU cutoff
            else
                buf[x] = " "
            end
        end
        table.insert(lines, table.concat(buf))
    end
    return lines
end

----------------------------------------------------------------------
-- Refresh
----------------------------------------------------------------------

local warned_no_rpc = false

local function refresh()
    local info = btcui_rpc("getmempoolinfo")
    if info then
        mempool_summary:set({
            txcount    = info.size or 0,
            vsize      = (info.bytes or 0) / 1e6,
            total_fees = info.total_fee or 0,
            minrelay   = (info.minrelaytxfee or 0) * 1e5,  -- BTC/kvB → sat/vB
            mempoolmin = (info.mempoolminfee or 0) * 1e5,
        })
    end

    local ok, diagram = pcall(btcui_rpc, "getmempoolfeeratediagram")
    if not ok or not diagram then
        if not warned_no_rpc then
            btcui_error("getmempoolfeeratediagram failed — requires Bitcoin Core v31+")
            warned_no_rpc = true
        end
        return
    end

    -- Ensure (0, 0) origin.
    local pts = {}
    if #diagram == 0 or diagram[1].weight > 0 or diagram[1].fee > 0 then
        table.insert(pts, { weight = 0, fee = 0 })
    end
    for _, p in ipairs(diagram) do table.insert(pts, p) end

    local max_w = pts[#pts].weight
    local max_f = pts[#pts].fee

    -- Next-block summary: walk chunks (consecutive deltas) up to BLOCK_WU.
    local prev_w, prev_f = 0, 0
    local nb_w, nb_f, nb_cnt = 0, 0, 0
    local top_fr, tail_fr = 0, 0
    for i = 2, #pts do
        local dw = pts[i].weight - prev_w
        local df = pts[i].fee    - prev_f
        if nb_w + dw > BLOCK_WU then break end
        if dw > 0 then
            local fr = sat_per_vb(dw, df)
            nb_w   = nb_w + dw
            nb_f   = nb_f + df
            nb_cnt = nb_cnt + 1
            if top_fr == 0 then top_fr = fr end
            tail_fr = fr
        end
        prev_w, prev_f = pts[i].weight, pts[i].fee
    end
    block_summary:set({
        top_feerate  = top_fr,
        tail_feerate = tail_fr,
        chunk_count  = nb_cnt,
        weight_mwu   = nb_w / 1e6,
        fees         = nb_f,
        fill_pct     = (nb_w / BLOCK_WU) * 100,
    })

    -- X-axis extends to at least one block, so the 4 MWU cutoff is
    -- always on-screen at a fixed relative position when there's less
    -- than a block in the mempool.
    local x_extent = math.max(max_w, BLOCK_WU)
    local y_extent = max_f > 0 and max_f or 1

    local block_col
    if x_extent > 0 then
        local c = math.floor(BLOCK_WU / x_extent * PLOT_WIDTH + 0.5)
        if c >= 1 and c <= PLOT_WIDTH then block_col = c end
    end

    local lines = render_plot(pts, x_extent, y_extent, block_col)
    local color = feerate_color(top_fr) or "cyan"

    -- Pre-pad every row to AXIS_PAD columns so the Y-labels + border,
    -- axis line, and X labels line up vertically.
    local function lpad(s, n)
        if #s >= n then return s end
        return string.rep(" ", n - #s) .. s
    end

    diagram_table:start_refresh()
    local mid_y = math.floor(PLOT_HEIGHT / 2) + 1
    for y = 1, PLOT_HEIGHT do
        local label
        if     y == 1           then label = fmt_btc(max_f)
        elseif y == mid_y       then label = fmt_btc(max_f / 2)
        elseif y == PLOT_HEIGHT then label = fmt_btc(0)
        else                         label = "" end
        -- "<label> │<plot>" — the │ sits at column AXIS_PAD, and the
        -- plot's first cell sits at column AXIS_PAD+1, directly under
        -- the first X-axis label char.
        local prefix = lpad(label, AXIS_PAD - 2) .. " │"
        diagram_table:update(y, {
            line = { value = prefix .. lines[y], color = color },
        })
    end

    -- Axis line: "       └──────…──"
    diagram_table:update(PLOT_HEIGHT + 1, {
        line = {
            value = string.rep(" ", AXIS_PAD - 1) .. "└" .. string.rep("─", PLOT_WIDTH),
            color = "gray",
        },
    })

    -- X-axis labels: "0" at the leftmost plot column, overall max at the
    -- right edge, and "4 MWU" under the cutoff (if it's visibly before
    -- the right label).
    do
        local left  = "0"
        local right = fmt_mwu(x_extent)
        local xbuf  = {}
        for i = 1, PLOT_WIDTH do xbuf[i] = " " end
        for i = 1, #left do xbuf[i] = left:sub(i, i) end
        for i = 1, #right do
            local pos = PLOT_WIDTH - #right + i
            if pos >= 1 and pos <= PLOT_WIDTH then xbuf[pos] = right:sub(i, i) end
        end
        if block_col and block_col + #("4 MWU") < PLOT_WIDTH - #right then
            local tag = "4 MWU"
            for i = 1, #tag do
                local pos = block_col + i - 1
                if pos >= 1 and pos <= PLOT_WIDTH then xbuf[pos] = tag:sub(i, i) end
            end
        end
        diagram_table:update(PLOT_HEIGHT + 2, {
            line = {
                value = string.rep(" ", AXIS_PAD) .. table.concat(xbuf),
                color = "gray",
            },
        })
    end
    diagram_table:finish_refresh()

    diagram_table:set_header_info({
        value = string.format("weight 0 → %s   fee 0 → %s BTC   top %.2f → tail %.2f sat/vB",
                              fmt_mwu(x_extent), fmt_btc(max_f), top_fr, tail_fr),
        color = "gray",
    })
end

local timer = btcui_set_interval(REFRESH, refresh)

btcui_add_footer_button(" ↺ refresh", function() btcui_wake(timer) end)

--- bitcoin-tui Settings
--- Auto-loaded at startup. Scans lua/tabs/ and lets you enable/disable tabs,
--- and toggle boolean options (e.g. hiding this Settings tab itself).
--- Usage: ↓ focus a list, Enter to select a row, then Enter (or click a row)
---        to toggle it on/off.  [r] refresh list
--- Note: hiding the Settings tab takes effect immediately; re-enable it by
---       starting with --settingstab=true (or removing settingstab from config).

btcui_set_name("Settings")

local config_path = btcui_config_path()
local script_dir  = btcui_script_dir()  -- directory containing this file (lua/tabs/)
local status_msg  = ""

-- ── Panels ────────────────────────────────────────────────────────────────

local tabs_panel = btcui_table({
    title   = "Available Lua Tabs",
    key     = "path",
    columns = {
        { name = "path", header = "" },        -- hidden key column (must be in columns list)
        { name = "on",   header = "On" },
        { name = "name", header = "Tab name" },
        { name = "file", header = "File" },
    },
})

-- Boolean options toggled the same way as the tab list (Enter / click a row).
-- Keys are prefixed "opt:" so toggle_selected can tell them apart from tab paths.
local opts_panel = btcui_table({
    title   = "Options",
    key     = "key",
    columns = {
        { name = "key",   header = "" },        -- hidden key column
        { name = "on",    header = "On" },
        { name = "label", header = "Option" },
    },
})

local info_panel = btcui_summary({
    title   = "Configuration",
    new_row = true,
    fields  = {
        { name = "config_file", label = "Config file" },
        { name = "status",      label = "Status" },
    },
})

-- ── Helpers ───────────────────────────────────────────────────────────────

-- Returns {path = true} set of enabled script paths and the full config table
local function get_config()
    local cfg     = btcui_config_read()
    local enabled = {}
    for _, spec in ipairs(cfg.tabs or {}) do
        local path = spec:match("^([^,]+)") or spec
        enabled[path] = true
    end
    return enabled, cfg
end

-- A cell rendering an on/off boolean.
local function on_cell(on)
    return on and { value = "✓", color = "green", bold = true }
              or  { value = "✗", color = "gray" }
end

-- Boolean options shown in opts_panel. Each has a config key, label, a getter
-- reading the current value from the config table, and a setter mutating it.
-- settingstab defaults to true (visible) when absent from config.
local OPTIONS = {
    {
        key   = "settingstab",
        label = "Show Settings tab (re-enable with --settingstab=true)",
        get   = function(cfg) return cfg.settingstab ~= false end,
        set   = function(cfg, v) cfg.settingstab = v end,
    },
}

local function refresh_display()
    local enabled, cfg = get_config()
    local files = btcui_list_files(script_dir)

    tabs_panel:start_refresh()
    for _, f in ipairs(files) do
        if f.name ~= "settings" then  -- hide self from the list
            local on = enabled[f.path]
            tabs_panel:update(f.path, {
                path = f.path,
                on   = on_cell(on),
                name = f.name,
                file = f.name .. ".lua",
            })
        end
    end
    tabs_panel:finish_refresh()

    opts_panel:start_refresh()
    for _, o in ipairs(OPTIONS) do
        opts_panel:update(o.key, {
            key   = o.key,
            on    = on_cell(o.get(cfg)),
            label = o.label,
        })
    end
    opts_panel:finish_refresh()

    local file_label
    if config_path == "" then
        file_label = { value = "(platform config dir not found)", color = "red" }
    elseif not cfg.exists then
        file_label = { value = config_path .. "  (not created yet)", color = "yellow" }
    else
        file_label = config_path
    end

    info_panel:set({
        config_file = file_label,
        status      = status_msg ~= "" and { value = status_msg, color = "cyan" } or "",
    })
end

-- Find an option definition by its config key.
local function find_option(key)
    for _, o in ipairs(OPTIONS) do
        if o.key == key then return o end
    end
end

-- Persist cfg and live-reload the tab bar; returns false (with status set) on error.
local function save_config(cfg)
    if not btcui_config_write(cfg) then
        status_msg = "ERROR: could not write " .. config_path
        return false
    end
    btcui_reload_tabs()   -- live-reloads the tab bar without restarting
    return true
end

-- Toggle a boolean option row (Options panel).
local function toggle_option(opt)
    local _, cfg = get_config()
    local now    = not opt.get(cfg)
    opt.set(cfg, now)
    status_msg = (now and "Enabled: " or "Disabled: ") .. opt.label
    if not save_config(cfg) then
        refresh_display()
        return
    end
    refresh_display()
end

-- Called by btcui_on_select with the activated row key (Enter or mouse click).
local function toggle_selected(key)
    key = key or tabs_panel:selected_key()
    if not key then
        status_msg = "Select a row first  (↓ focus, Enter select)"
        refresh_display()
        return
    end

    -- Options panel rows match an OPTIONS entry; everything else is a tab path.
    local opt = find_option(key)
    if opt then
        toggle_option(opt)
        return
    end

    local enabled, cfg = get_config()
    local tabs = cfg.tabs or {}

    if enabled[key] then
        -- Remove from config
        for i, spec in ipairs(tabs) do
            local path = spec:match("^([^,]+)") or spec
            if path == key then
                table.remove(tabs, i)
                break
            end
        end
        status_msg = "Disabled."
    else
        -- Add to config
        table.insert(tabs, key)
        status_msg = "Enabled."
    end

    cfg.tabs = tabs
    save_config(cfg)
    refresh_display()
end

-- ── Interaction ───────────────────────────────────────────────────────────

-- Enter on a selected row, or a mouse click on a row, toggles that tab.
btcui_on_select(toggle_selected)

btcui_add_footer_button("[r]efresh", function()
    status_msg = ""
    refresh_display()
end)

-- ── Initial load ──────────────────────────────────────────────────────────
refresh_display()

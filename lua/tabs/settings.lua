--- bitcoin-tui Settings
--- Auto-loaded at startup. Scans lua/tabs/ and lets you enable/disable tabs.
--- Usage: ↓ focus the list, Enter to select a row, then Enter (or click a row)
---        to toggle it on/off.  [r] refresh list

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

local function refresh_display()
    local enabled, cfg = get_config()
    local files = btcui_list_files(script_dir)

    tabs_panel:start_refresh()
    for _, f in ipairs(files) do
        if f.name ~= "settings" then  -- hide self from the list
            local on = enabled[f.path]
            tabs_panel:update(f.path, {
                path = f.path,
                on   = on and { value = "✓", color = "green", bold = true }
                          or  { value = "✗", color = "gray" },
                name = f.name,
                file = f.name .. ".lua",
            })
        end
    end
    tabs_panel:finish_refresh()

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

-- Called by btcui_on_select with the activated row key (Enter or mouse click).
local function toggle_selected(key)
    key = key or tabs_panel:selected_key()
    if not key then
        status_msg = "Select a tab first  (↓ focus, Enter select)"
        refresh_display()
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
    if not btcui_config_write(cfg) then
        status_msg = "ERROR: could not write " .. config_path
        refresh_display()
        return
    end
    btcui_reload_tabs()   -- live-reloads the tab bar without restarting
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

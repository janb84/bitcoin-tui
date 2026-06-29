--- bitcoin-tui Settings
--- Auto-loaded at startup. Scans lua/tabs/ and lets you enable/disable tabs,
--- and toggle boolean options (e.g. hiding this Settings tab itself).
--- Usage: ↓ focus a list, Enter to start selecting, then Space toggles a row
---        on/off; Enter activates the Debug file row (opens a path dialog).
---        Clicking a row does whichever its type offers.  [r] refresh list
--- Note: hiding the Settings tab takes effect immediately; re-enable it by
---       starting with --settingstab=true (or removing settingstab from config).

-- Lua 5.5 strict globals: declaring every global we reference voids
-- global-by-default for this chunk, so a typo in any btcui_* or stdlib
-- name is caught at load time instead of failing silently at runtime.
global btcui_add_footer_button, btcui_config_path, btcui_config_read,
       btcui_config_write, btcui_debug_active, btcui_list_files, btcui_on_select,
       btcui_reload_tabs, btcui_script_dir, btcui_set_name, btcui_summary,
       btcui_table, btcui_text_input, ipairs, table

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
        { name = "config_file",   label = "Config file" },
        { name = "debug_logging", label = "Debug logging" },
        { name = "status",        label = "Status" },
    },
})

-- Sentinel row key for the "Debug file" action row in the Options panel. It is
-- not a boolean option — activating it (Enter/Space/click) opens a path dialog.
local DEBUG_FILE_KEY = "action:debug_file"

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
    {
        key        = "debug",
        label      = "Enable debug logging",
        apply_note = "restart to apply",
        get        = function(cfg) return cfg.debug == true end,
        set        = function(cfg, v) cfg.debug = v end,
        -- Enabling debug requires a target file (mirrors --debug needs --debug-file).
        can_enable = function(cfg)
            return cfg.debug_file ~= nil and cfg.debug_file ~= ""
        end,
        enable_hint = "Set the Debug file row first before enabling debug logging.",
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
    -- Action row (not a toggle): navigate to it and press Enter/Space to edit.
    local file_shown = (cfg.debug_file ~= nil and cfg.debug_file ~= "")
                       and cfg.debug_file or "(none — Enter/Space to set)"
    opts_panel:update(DEBUG_FILE_KEY, {
        key   = DEBUG_FILE_KEY,
        on    = { value = "✎", color = "cyan", bold = true },
        label = "Debug file: " .. file_shown,
    })
    opts_panel:finish_refresh()

    local file_label
    if config_path == "" then
        file_label = { value = "(platform config dir not found)", color = "red" }
    elseif not cfg.exists then
        file_label = { value = config_path .. "  (not created yet)", color = "yellow" }
    else
        file_label = config_path
    end

    -- `cfg.debug` is the persisted/desired state; btcui_debug_active() is what is
    -- actually running this session (logging starts only at launch). When they
    -- disagree, the change is pending a restart — say so explicitly.
    local active = btcui_debug_active()
    local debug_label
    if cfg.debug and active then
        debug_label = { value = "active", color = "green" }
    elseif cfg.debug and not active then
        debug_label = { value = "enabled — restart to start logging", color = "yellow" }
    elseif not cfg.debug and active then
        debug_label = { value = "disabled — restart to stop logging", color = "yellow" }
    else
        debug_label = { value = "off", color = "gray" }
    end

    info_panel:set({
        config_file   = file_label,
        debug_logging = debug_label,
        status        = status_msg ~= "" and { value = status_msg, color = "cyan" } or "",
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

-- Open the path dialog and persist the entered location. Changing the file
-- always turns logging off — re-enable it with the "Enable debug logging" row
-- once the path is right.
local function prompt_debug_file()
    local _, cfg = get_config()
    btcui_text_input("Debug log file path (empty to clear):", cfg.debug_file or "",
        function(path)
            if path == nil then return end   -- cancelled
            local _, cfg2 = get_config()
            cfg2.debug_file = path
            cfg2.debug      = false   -- changing the file disables logging; re-enable it
            if path == "" then
                status_msg = "Debug file cleared; logging disabled."
            else
                status_msg = "Debug file set: " .. path .. "  (enable logging to use it)"
            end
            save_config(cfg2)
            refresh_display()
        end)
end

-- Toggle a boolean option row (Options panel).
local function toggle_option(opt)
    local _, cfg = get_config()
    local now    = not opt.get(cfg)
    -- Refuse to enable an option whose precondition isn't met (e.g. debug
    -- logging without a target file), which would otherwise break startup.
    if now and opt.can_enable and not opt.can_enable(cfg) then
        status_msg = opt.enable_hint or "Cannot enable this option yet."
        refresh_display()
        return
    end
    opt.set(cfg, now)
    status_msg = (now and "Enabled: " or "Disabled: ") .. opt.label
    if opt.apply_note then
        status_msg = status_msg .. "  (" .. opt.apply_note .. ")"
    end
    if not save_config(cfg) then
        refresh_display()
        return
    end
    refresh_display()
end

-- Called by btcui_on_select with the row key and trigger:
--   "enter"/"click" → activate (open the Debug file dialog)
--   "space"/"click" → toggle (boolean options and tab on/off)
-- Click does whichever action the row offers; Enter and Space are kept distinct.
local function toggle_selected(key, trigger)
    key = key or tabs_panel:selected_key()
    if not key then
        status_msg = "Select a row first  (↓ focus, Enter select)"
        refresh_display()
        return
    end

    local activate = (trigger == "enter" or trigger == "click")
    local toggle   = (trigger == "space" or trigger == "click")

    -- The debug-file action row opens the path dialog (activate).
    if key == DEBUG_FILE_KEY then
        if activate then
            prompt_debug_file()
        else
            status_msg = "Press Enter to set the debug file."
            refresh_display()
        end
        return
    end

    -- Every other row is a boolean toggle (an option or a tab on/off).
    if not toggle then
        status_msg = "Press Space to toggle this row on/off."
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

-- Space toggles a selected row; Enter activates it; click does its action.
btcui_on_select(toggle_selected)

btcui_add_footer_button("[r]efresh", function()
    status_msg = ""
    refresh_display()
end)

-- ── Initial load ──────────────────────────────────────────────────────────
refresh_display()

--- bitcoin-tui Lua Tab API
--- Type definitions for IDE autocompletion (LuaLS / lua-language-server).
--- Drop this file into your workspace so your editor knows the API.
--- Requires a Lua 5.5 runtime setting (the `btcui_*` API is declared with the
--- 5.5 `global` keyword).
---
--- Strict globals (optional, recommended): bitcoin-tui runs on Lua 5.5, so a tab
--- script can declare the names it uses up front to void global-by-default — then
--- a typo in any btcui_* or stdlib name is a load-time error instead of a silent
--- nil. The bundled tabs in lua/tabs/ do this; e.g.:
---   global btcui_table, btcui_summary, btcui_set_name, ipairs, string

----------------------------------------------------------------------
-- Global functions — available at top level and in callbacks
----------------------------------------------------------------------

--- Create a managed table for display. Returns a Table object.
--- Options:
---   key       string   Column name used as row key (default: first column)
---   title     string   Section title (default: "Lua Table")
---   no_header boolean  Hide the header row (default: false)
---   columns   ColumnDef[]  Column definitions (required)
---@param opts TableOpts
---@return Table
global function btcui_table(opts) end

--- Register a periodic timer callback. The callback runs as a
--- coroutine — btcui_rpc() yields transparently within it.
--- Returns an opaque TimerHandle that can be passed to btcui_wake().
---@param seconds number   Interval in seconds
---@param callback function  Called each interval
---@return TimerHandle
global function btcui_set_interval(seconds, callback) end

--- Wake a timer so it fires on the next loop iteration, regardless
--- of its normal interval. If the timer's callback is currently
--- running (waiting for an RPC), the wake is deferred until the
--- current invocation finishes.
---@param handle TimerHandle
global function btcui_wake(handle) end

--- Set the tab name displayed in the tab bar. Can only be called
--- during script loading (top-level code); calling it from a
--- callback raises an error. The name takes effect immediately,
--- so it persists even if the script fails to load.
--- Note: if the user specified t=... in the --tab option, that
--- takes precedence over btcui_set_name().
---@param name string
global function btcui_set_name(name) end

--- Read a tab option set via --tab script.lua,key=val,...
--- Returns the string value if set. If not set, returns default_val.
--- If not set and no default is provided, raises an error.
--- The "t" key is reserved for the tab title override.
---@param key string
---@param default_val? any   Returned when the option is not set
---@return any
global function btcui_option(key, default_val) end

--- Register a log pattern callback. The pattern uses RE2 syntax and
--- is matched against the message portion of each debug.log line.
--- Callback receives (timestamp, message, capture1, capture2, ...).
--- Log callbacks are plain function calls — they cannot call btcui_rpc().
---@param pattern string           RE2 pattern (capture groups become extra args)
---@param callback function        fn(ts, msg, ...)
---@param backlog? integer         Bytes of historical log to process (default: 0)
global function btcui_watch_log(pattern, callback, backlog) end

--- Call a Bitcoin Core RPC method. Can only be called from within a
--- btcui_set_interval callback (yields the coroutine). The RPC is
--- dispatched to a background thread, so other timers and log
--- callbacks continue to run while waiting. Returns the parsed
--- JSON result directly. On RPC error, raises a Lua error (catchable
--- with pcall/xpcall).
---@param method string   RPC method name (must be in the allowlist)
---@param ... any         Method parameters
---@return any
global function btcui_rpc(method, ...) end

--- Call a Bitcoin Core RPC method for a specific wallet. Can only be called from within a
--- btcui_set_interval callback (yields the coroutine). The RPC is
--- dispatched to a background thread, so other timers and log
--- callbacks continue to run while waiting. Returns the parsed
--- JSON result directly. On RPC error, raises a Lua error (catchable
--- with pcall/xpcall).
---@param wallet string   Wallet name (empty string for the default wallet)
---@param method string   RPC method name (must be in the allowlist)
---@param ... any         Method parameters
---@return any
global function btcui_rpc_wallet(wallet, method, ...) end

--- Create a summary panel for display. Returns a Summary object.
--- Summary panels show compact "Label : Value" lines and are
--- rendered side-by-side when consecutive summaries are created.
--- Options:
---   title   string        Section title (default: "Summary")
---   fields  FieldDef[]    Field definitions (required)
---@param opts SummaryOpts
---@return Summary
global function btcui_summary(opts) end

--- Set the status line hint text (displayed in the tab bar).
---@param text string
global function btcui_key_hint(text) end

--- Display a warning message in the ERRORS pane. The message includes
--- the caller's source location and ages out after 20 seconds.
---@param msg string
global function btcui_error(msg) end

--- Register a clickable button in the footer bar. Can only be called
--- during script loading (top-level code). The callback is invoked on
--- the Lua thread when the button is clicked.
--- An optional keyboard shortcut can be provided as the third argument.
--- If omitted, the key is auto-extracted from the label when it contains
--- a "[x]" pattern (e.g. "[r] Refresh" → key "r").
---@param label string       Button label text (e.g. "[r] Refresh")
---@param callback function  Called when the button is clicked or key is pressed
---@param key? string        Optional explicit keyboard shortcut (single character)
global function btcui_add_footer_button(label, callback, key) end

--- Open a QR code overlay. Accepts either a single string or a list of
--- items for a tabbed overlay. The overlay replaces the footer with a
--- single "[Esc] Close" button; press Esc to dismiss.
--- QR codes use error-correction level MEDIUM.
---
---   -- Single QR code:
---   btcui_open_qr_overlay("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq")
---
---   -- Tabbed (multiple QR codes):
---   btcui_open_qr_overlay({
---     { label = "Address", data = addr },
---     { label = "TXID",    data = txid },
---   })
---
---@param data string|{label:string, data:string}[]
global function btcui_open_qr_overlay(data) end

--- Show or hide the global "/ search" button in the footer bar for this tab.
--- Defaults to true. Can be called at any time (e.g. when an overlay is active).
---@param show boolean
global function btcui_show_search_button(show) end

--- Show or hide the global "[q] quit" button in the footer bar for this tab.
--- Defaults to true. Can be called at any time.
---@param show boolean
global function btcui_show_quit_button(show) end

--- Return the current terminal dimensions in cells (width, height).
--- The value reflects the most recent render and updates as the user
--- resizes the terminal. Useful for sizing plots or laying out
--- variable-width content. Pair with btcui_on_resize() to react to
--- changes immediately instead of on the next refresh tick.
---@return integer width
---@return integer height
global function btcui_screen_size() end

--- Register a callback that fires whenever the terminal is resized.
--- The callback receives the new (width, height). Only one handler
--- can be registered — a second call replaces the previous one.
--- A common pattern is to wake a refresh timer:
---
---   local timer = btcui_set_interval(5, refresh)
---   btcui_on_resize(function(w, h) btcui_wake(timer) end)
---
---@param callback fun(width: integer, height: integer)
global function btcui_on_resize(callback) end

--- Register a callback that fires when a table row is activated — either by
--- pressing Enter while the row is selected, or by clicking the row with the
--- mouse. The callback receives the activated row's key (as a string). Row
--- selection mode stays active, so several rows can be activated in sequence.
--- Only one handler can be registered — a second call replaces the previous one.
---
---   btcui_on_select(function(key)
---     -- act on the row identified by `key`
---   end)
---
---@param callback fun(key: string)
global function btcui_on_select(callback) end

--- Return a styled address cell value. The address is rendered with
--- alternating bold groups of 4 characters (e.g. "bc1q ar0s rr7x …"),
--- making it easier to scan visually. Pass the result as a cell value
--- in Table:update() or Summary:set() wherever you would use a plain
--- address string.
---
---   -- Example:
---   address = btcui_address(utxo.address or "")
---
---@param addr string   Raw address string (empty string renders as "N/A")
---@return StyledAddress
global function btcui_address(addr) end

--- Opaque styled-address value returned by btcui_address().
--- Pass it wherever a cell value is accepted.
---@class StyledAddress

--- Return a progress-bar cell value. The bar fills its column and shows a
--- trailing percentage; frac is clamped to [0, 1]. Pass the result as a cell
--- value in Table:update() or Summary:set() wherever you would use a value.
--- Options:
---   color   string   Bar/percentage color (default: "cyan")
---   prefix  string   Bold label drawn before the bar (e.g. "1.2 MB / 300 MB")
---
---   -- Example:
---   sync = btcui_gauge(0.99, { color = "green" })
---   mem  = btcui_gauge(used / max, { color = "yellow", prefix = "12 MB / 300 MB" })
---
---@param frac number      Fraction in [0, 1]
---@param opts? { color?: string, prefix?: string }
---@return StyledGauge
global function btcui_gauge(frac, opts) end

--- Opaque styled-gauge value returned by btcui_gauge().
--- Pass it wherever a cell value is accepted.
---@class StyledGauge

----------------------------------------------------------------------
-- Timer handle
----------------------------------------------------------------------

--- Opaque handle returned by btcui_set_interval, used with btcui_wake.
---@class TimerHandle

----------------------------------------------------------------------
-- Table object
----------------------------------------------------------------------

---@class Table
local Table = {}

--- Insert or update a row by key. Values can be plain values or
--- styled tables with { value = v, color = "red", bold = true }.
--- Nil values are skipped (column keeps its previous value).
---@param key any       Key value (matches key column type)
---@param data table    Column values as { name = value, ... }
function Table:update(key, data) end

--- Remove a row by key. Returns true if a row was removed.
---@param key any
---@return boolean
function Table:remove(key) end

--- Return all current keys as an array of strings.
---@return string[]
function Table:keys() end

--- Return the key of the currently selected row as a string, or nil if
--- no row is selected. Row selection is controlled by the user with the
--- keyboard: ↓ to focus the panel, Enter to enter selection mode,
--- ↑/↓ to move the selection, Esc to exit selection mode.
---@return string|nil
function Table:selected_key() end

--- Return the formatted value of a named column in the currently selected
--- row, or nil if no row is selected or the column does not exist.
--- Address cells (created with btcui_address()) return the raw address string.
---@param column string   Column name as defined in the columns table
---@return string|nil
function Table:selected_value(column) end

--- Start a refresh cycle. Bumps the internal epoch counter.
--- Subsequent update() calls stamp rows with the new epoch.
--- Call finish_refresh() after all updates to remove stale rows.
function Table:start_refresh() end

--- Finish a refresh cycle. Removes all rows whose epoch does not
--- match the current epoch (i.e., rows not touched since the last
--- start_refresh() call).
function Table:finish_refresh() end

--- Set extra info text displayed next to the table title.
--- Accepts a plain string or a styled table { value = str, color = "...", bold = true }.
--- Set to "" to clear.
---@param info string|StyledValue
function Table:set_header_info(info) end

----------------------------------------------------------------------
-- Summary object
----------------------------------------------------------------------

---@class Summary
local Summary = {}

--- Update one or more fields. Values can be plain values or
--- styled tables with { value = v, color = "red", bold = true }.
--- Only named fields are updated; others keep their previous value.
---@param data table    Field values as { name = value, ... }
function Summary:set(data) end

----------------------------------------------------------------------
-- Summary options
----------------------------------------------------------------------

---@class SummaryOpts
---@field title?    string       Section title (default: "Summary")
---@field new_row?  boolean      Start a new side-by-side row instead of joining
---                              the run of preceding summaries (default: false)
---@field fields    FieldDef[]   Field definitions

---@class FieldDef
---@field name      string   Field identifier, used as key in set() data tables.
---@field label     string   Display label shown before the value.
---@field type?     string   "string" (default), "number", or a time format.
---@field decimals? integer  Fixed decimal places for number fields (-1 = auto).

----------------------------------------------------------------------
-- Table options
----------------------------------------------------------------------

---@class TableOpts
---@field key?       string       Key column name (default: first column)
---@field title?     string       Section title
---@field no_header? boolean      Hide header row
---@field columns    ColumnDef[]  Column definitions

----------------------------------------------------------------------
-- Column definitions
----------------------------------------------------------------------

---@class ColumnDef
---@field name      string   Column identifier, used as key in update() data tables.
---@field header    string   Header text. Use \n for multi-line. Empty string hides the column.
---@field type?     string   "string" (default), "number", or a time format.
---@field decimals? integer  Fixed decimal places for number columns (-1 = auto).

----------------------------------------------------------------------
-- Settings / config helpers
----------------------------------------------------------------------

--- Return the directory that contains the currently running Lua script.
--- Useful for locating sibling scripts or data files. Returns "" when the
--- script path is unavailable.
---@return string
global function btcui_script_dir() end

--- List all .lua files in the given directory. Returns an array of
--- {name, path} tables sorted alphabetically by name, where `name` is
--- the filename stem (no extension) and `path` is the absolute path.
--- Returns an empty array when the directory does not exist.
---@param dir string   Absolute path to the directory to scan
---@return {name: string, path: string}[]
global function btcui_list_files(dir) end

--- Return the absolute path to the bitcoin-tui config file (config.toml),
--- or an empty string when the platform config directory cannot be determined.
---@return string
global function btcui_config_path() end

--- Read config.toml and return the relevant fields as a Lua table.
--- Returns defaults when the file does not exist or a field is absent.
--- Table shape:
---   exists     boolean    true when config.toml was found and read; false when using defaults
---   tabs       string[]   list of --tab specs  (e.g. {"script.lua", "other.lua,opt=v"})
---   allow_rpc  string[]   extra RPC method names added via --allow-rpc
---   refresh    integer    refresh interval in seconds (default: 5)
---   host       string     RPC host (default: "127.0.0.1")
---   port       integer    RPC port (default: 8332)
---@return { exists: boolean, tabs: string[], allow_rpc: string[], refresh: integer, host: string, port: integer }
global function btcui_config_read() end

--- Write config.toml, updating the managed keys (tabs, allow_rpc, refresh)
--- while preserving all other settings (host, port, credentials, etc.).
--- Creates the config directory if it does not exist.
--- Returns true on success, false when the file cannot be written.
---
--- Table shape (same as btcui_config_read()):
---   tabs       string[]   new list of --tab specs
---   allow_rpc  string[]   extra RPC methods (optional)
---   refresh    integer    refresh interval (optional)
---
--- After writing, call btcui_reload_tabs() to apply tab changes live (no
--- restart needed). Other settings (refresh, allow_rpc) still take effect on
--- the next restart.
---@param cfg { tabs: string[], allow_rpc?: string[], refresh?: integer }
---@return boolean
global function btcui_config_write(cfg) end

--- Re-read config.toml and reconcile the running Lua tabs with its `tab` list:
--- newly added tabs are loaded, removed tabs are unloaded, and surviving tabs
--- keep their state. Auto-injected tabs (e.g. Settings) are never removed.
--- Typically called right after btcui_config_write() to apply changes live.
--- Safe to call from footer-button and timer callbacks.
global function btcui_reload_tabs() end

--- Show a modal text-input overlay. The callback receives the entered string on
--- confirm (Enter), or nil when the user cancels (Esc). Can be called from
--- footer-button callbacks and timer callbacks (not from coroutines mid-RPC).
--- Only one input overlay can be active at a time; a second call replaces it.
---
---   btcui_text_input("Script path:", "", function(path)
---     if path == nil then return end   -- cancelled
---     -- use path …
---   end)
---
---@param label     string     Prompt text shown above the input field
---@param default   string     Pre-filled value (use "" for an empty field)
---@param on_confirm fun(value: string|nil)  Called with the typed string or nil
global function btcui_text_input(label, default, on_confirm) end

--- Column types:
---   "string"    — displayed as-is, left-aligned
---   "number"    — right-aligned; use decimals for fixed precision
---   "datetime"  — unix timestamp displayed as YYYY-MM-DD HH:MM:SS
---   "date"      — unix timestamp displayed as YYYY-MM-DD
---   "time"      — unix timestamp displayed as HH:MM:SS
---   "time_ms"   — unix timestamp displayed as HH:MM:SS.mmm
---   "timestamp" — alias for "time_ms" (deprecated)

----------------------------------------------------------------------
-- Cell values
----------------------------------------------------------------------

--- Cell values in Table:update() can be plain values, styled tables,
--- or the result of btcui_address():
---
---   -- Plain value:
---   height = 890123
---
---   -- Styled value:
---   height = { value = 890123, color = "green", bold = true }
---
---   -- Formatted address (alternating bold groups of 4):
---   address = btcui_address(utxo.address or "")
---
--- Available colors: "red", "green", "yellow", "cyan", "gray"
---
--- Nil values are skipped (the cell retains its previous value).
--- For numeric columns, unset cells render as blank.

----------------------------------------------------------------------
-- RPC allowlist
----------------------------------------------------------------------

--- The following read-only RPC methods are permitted via btcui_rpc() and btcui_rpc_wallet():
---
--- Blockchain:
---   getbestblockhash, getblock, getblockchaininfo, getblockcount,
---   getblockhash, getblockheader, getblockstats, getchaintips,
---   getdeploymentinfo, getindexinfo, gettxout, gettxoutsetinfo
---
--- Mempool:
---   getmempoolinfo, getrawmempool, getmempoolentry,
---   getmempoolancestors, getmempooldescendants,
---   getmempoolcluster, getmempoolfeeratediagram
---
--- Network:
---   getpeerinfo, getnetworkinfo, getnettotals,
---   getconnectioncount, getnodeaddresses
---
--- Mining:
---   getmininginfo, getnetworkhashps
---
--- Util:
---   estimatesmartfee, uptime, logging
---
--- Raw transactions (read-only):
---   getrawtransaction, decoderawtransaction, decodescript

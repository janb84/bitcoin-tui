--- bitcoin-tui Lua Tab API
--- Type definitions for IDE autocompletion (LuaLS / lua-language-server).
--- Drop this file into your workspace so your editor knows the API.

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
function btcui_table(opts) end

--- Register a periodic timer callback. The callback runs as a
--- coroutine — btcui_rpc() yields transparently within it.
--- Returns an opaque TimerHandle that can be passed to btcui_wake().
---@param seconds number   Interval in seconds
---@param callback function  Called each interval
---@return TimerHandle
function btcui_set_interval(seconds, callback) end

--- Wake a timer so it fires on the next loop iteration, regardless
--- of its normal interval. If the timer's callback is currently
--- running (waiting for an RPC), the wake is deferred until the
--- current invocation finishes.
---@param handle TimerHandle
function btcui_wake(handle) end

--- Set the tab name displayed in the tab bar. Can only be called
--- during script loading (top-level code); calling it from a
--- callback raises an error. The name takes effect immediately,
--- so it persists even if the script fails to load.
--- Note: if the user specified t=... in the --tab option, that
--- takes precedence over btcui_set_name().
---@param name string
function btcui_set_name(name) end

--- Read a tab option set via --tab script.lua,key=val,...
--- Returns the string value if set. If not set, returns default_val.
--- If not set and no default is provided, raises an error.
--- The "t" key is reserved for the tab title override.
---@param key string
---@param default_val? any   Returned when the option is not set
---@return any
function btcui_option(key, default_val) end

--- Register a log pattern callback. The pattern uses RE2 syntax and
--- is matched against the message portion of each debug.log line.
--- Callback receives (timestamp, message, capture1, capture2, ...).
--- Log callbacks are plain function calls — they cannot call btcui_rpc().
---@param pattern string           RE2 pattern (capture groups become extra args)
---@param callback function        fn(ts, msg, ...)
---@param backlog? integer         Bytes of historical log to process (default: 0)
function btcui_watch_log(pattern, callback, backlog) end

--- Call a Bitcoin Core RPC method. Can only be called from within a
--- btcui_set_interval callback (yields the coroutine). The RPC is
--- dispatched to a background thread, so other timers and log
--- callbacks continue to run while waiting. Returns the parsed
--- JSON result directly. On RPC error, raises a Lua error (catchable
--- with pcall/xpcall).
---@param method string   RPC method name (must be in the allowlist)
---@param ... any         Method parameters
---@return any
function btcui_rpc(method, ...) end

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
function btcui_rpc_wallet(wallet, method, ...) end

--- Create a summary panel for display. Returns a Summary object.
--- Summary panels show compact "Label : Value" lines and are
--- rendered side-by-side when consecutive summaries are created.
--- Options:
---   title   string        Section title (default: "Summary")
---   fields  FieldDef[]    Field definitions (required)
---@param opts SummaryOpts
---@return Summary
function btcui_summary(opts) end

--- Set the status line hint text (displayed in the tab bar).
---@param text string
function btcui_key_hint(text) end

--- Display a warning message in the ERRORS pane. The message includes
--- the caller's source location and ages out after 20 seconds.
---@param msg string
function btcui_error(msg) end

--- Register a clickable button in the footer bar. Can only be called
--- during script loading (top-level code). The callback is invoked on
--- the Lua thread when the button is clicked.
---@param label string     Button label text
---@param callback function  Called when the button is clicked
function btcui_add_footer_button(label, callback) end

--- Show or hide the global "/ search" button in the footer bar for this tab.
--- Defaults to true. Can be called at any time (e.g. when an overlay is active).
---@param show boolean
function btcui_show_search_button(show) end

--- Show or hide the global "[q] quit" button in the footer bar for this tab.
--- Defaults to true. Can be called at any time.
---@param show boolean
function btcui_show_quit_button(show) end

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
---@field title?  string       Section title (default: "Summary")
---@field fields  FieldDef[]   Field definitions

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

--- Cell values in Table:update() can be plain values or styled:
---
---   -- Plain value:
---   height = 890123
---
---   -- Styled value:
---   height = { value = 890123, color = "green", bold = true }
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

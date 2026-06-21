# Changelog

All notable changes to bitcoin-tui are documented here.

## [Unreleased]

### Added
- **Mouse support** - click tabs to switch between them
- **Clickable footer bar** - footer hints are now rendered as a dedicated mouse-aware footer bar; click refresh/search/quit and tab-specific actions directly
- **Lua scripting** - load custom tabs from Lua scripts with `--tab <path.lua>`; scripts can call a configurable set of RPC methods (allowlisted with `--allow-rpc`); optional debug log via `--debuglog`; bundled examples: slow-block monitor, wallet info, feerate diagram
- **Lua footer buttons** - Lua scripts can register clickable footer actions with `btcui_add_footer_button(label, callback)`; `btcui_show_search_button(bool)` and `btcui_show_quit_button(bool)` let scripts hide the global search and quit buttons per-tab
- **Config file support** - options can be set in a `config.toml` file (Linux: `$XDG_CONFIG_HOME/bitcoin-tui/` or `~/.config/bitcoin-tui/`; macOS: `~/Library/Application Support/bitcoin-tui/`; Windows: `%APPDATA%\bitcoin-tui\`); CLI flags override file values; path can be changed with `--config-file <path>` or `$BITCOIN_TUI_CONFIG_FILE`
- CLI11 replaces hand-rolled argument parsing; adds `--help` grouping, `--config-file` support, and stricter validation of unknown flags
- **Debug file output** — `--debug --debug-file <path>` writes internal debug data to a file (append mode); both flags must be used together
- **Lua footer button keyboard shortcuts** - `btcui_add_footer_button` now auto-extracts the keyboard shortcut from `[x]` patterns in the label (e.g. `"[r] QR"` binds the `r` key automatically); an explicit key can also be passed as the optional third argument
- **QR code overlay** - `btcui_open_qr_overlay(data)` opens a full-screen QR overlay; accepts a plain address string or a list of `{label, data}` items for a tabbed overlay; `left arrow/ right arrow` arrow keys switch between tabs; 
- **Lua table row selection** - users can select a row in any Lua table panel by pressing `arrow down` to focus the panel, `Enter` to enter selection mode, `arrow up / arrow down` to move the selection, and `Esc` to exit selection mode; selected row is highlighted; `Table:selected_key()` returns the key of the selected row and `Table:selected_value(column)` returns the formatted value of any column in that row, enabling Lua callbacks to act on the user's selection
- **Lua table row activation** - `btcui_on_select(callback)` fires when a row is activated, with its key; rows are now mouse-clickable too a first click selects the row, a second click on the same row activates it, mirroring the keyboard `Enter`-to-select then `Enter`-to-activate flow
- **Settings tab** - an auto-loaded Settings tab (`lua/tabs/settings.lua`) scans `lua/tabs/` and lets you enable/disable Lua tabs from the UI; selecting a tab writes `config.toml` and reloads the tab bar live, with no restart
- **Settings tab visibility** - the Settings tab can be hidden via `settingstab = false` in `config.toml` or the `--settingstab=true|false` CLI option (default `true`); the Settings tab also has an "Options" panel to toggle this boolean from the UI (hiding takes effect immediately; re-enable by starting with `--settingstab=true`)
- **Live tab reload** - `btcui_reload_tabs()` reconciles the running Lua tabs with `config.toml`'s `tab` list (loads added tabs, unloads removed ones, keeps survivors and their state); de-loaded tabs shut their worker threads down promptly
- **Lua config API** - `btcui_config_read()` / `btcui_config_write(cfg)` read and update the managed `config.toml` keys while preserving everything else; `btcui_config_path()`, `btcui_script_dir()`, and `btcui_list_files(dir)` help scripts locate the config file and sibling scripts
- **Lua text input** - `btcui_text_input(label, default, on_confirm)` shows a modal text-input overlay and calls back with the entered string (or nil on `Esc`)
- **testnet4 support** - `--testnet4` shortcut for connecting to the testnet4 network
- **Windows support** - native MSVC build target with bundled application icon, automatic detection of the default Windows bitcoin data directory, and a dedicated console window when launched from Explorer
- **Lua scrollable tables** - large Lua tables can now be scrolled when they exceed the available panel height
- **Lua summary panels** - Lua scripts can render compact key/value summary panels alongside tables
- **Lua untitled panels** - panels in Lua tabs can omit a title for a cleaner layout
- **Lua address element** - styled address rendering element exposed to the Lua API
- **Lua gauge element** - `btcui_gauge(frac, {color, prefix})` renders a progress bar cell (with trailing percentage) usable in any table or summary; backed by a reusable `gauge_element` component shared with the built-in Dashboard's mempool meter
- **Lua summary layout** - `btcui_summary({new_row = true})` breaks the side-by-side run of consecutive summaries, starting a new full-width row
- **Dashboard example** - `lua/examples/dashboard.lua` ports the built-in Dashboard tab to Lua, demonstrating the 2+1 summary layout and gauges
- **Lua wallet RPC selection** - `btcui_rpc_wallet(name)` directs subsequent RPC calls at a specific named wallet
- **Lua script options** - `btcui_option(name)` reads CLI/config-file values passed through to scripts
- **Lua timestamp formats** - multiple timestamp formats now exposed to Lua scripts
- **Lua tab directory override** - `--lua-dir <path>` (or `lua-dir` in `config.toml`) points at the `lua/` root; tab scripts are loaded from `<lua-dir>/tabs`, overriding the executable-relative auto-detection
- **Explicit config file location** - `--config-file <path>` and `$BITCOIN_TUI_CONFIG_FILE` set the exact `config.toml` path for both reading and writing, independent of `$HOME`/XDG; useful for service users without a home directory

### Changed
- The Network tab is no longer a built-in C++ tab; it has been removed from the executable and replaced by `lua/tabs/network.lua` (load on demand with `--tab`); `src/tabs/network.cpp` and `src/tabs/network.hpp` are no longer compiled
- FTXUI updated from v5.0.0 to v7.0.0;
- Footer hints are unified across tabs: context-sensitive actions now appear in one shared footer bar instead of each tab rendering its own status text
- FetchContent dependencies are now pinned by SHA256 hash for reproducibility
- When run via `sudo` as root, config and cache paths now resolve to the invoking user's home (`SUDO_USER`) instead of root's, and files created as root are chowned back to that user
- bitcoin-tui now exits with a clear error instead of guessing a location when no config directory can be determined (e.g. `sudo -u <user>` for a user with no home directory); pass `--config-file` or set `$BITCOIN_TUI_CONFIG_FILE`

### Fixed
- Lua tab auto-detection (including the Settings tab) no longer silently fails when bitcoin-tui is launched from `PATH` (e.g. under `sudo`); the executable path is now resolved from the OS rather than `argv[0]`


## [0.8.3] - 2026-04-11

### Fixed
- Guix release binaries are now stripped of debug symbols, reducing binary size.

## [0.8.2] - 2026-04-10

### Added
- Reproducible builds via GNU Guix (`contrib/guix/guix-build`) for Linux (x86\_64, arm), Windows (x86\_64), and macOS (x86\_64, aarch64)

### Fixed
- Show `--help` / `--version` output and exit without starting the TUI
- Cross-platform build support for Windows and macOS (Winsock, POSIX `#ifdef` guards)

## [0.8.1] - 2026-03-24

### Added
- **Launch bitcoind** — when the connection failed overlay is shown on localhost, bitcoin-tui now checks whether `bitcoind` is present in `PATH` (or an explicit `--bitcoind <path>` was given) and if present, offers a "Launch bitcoind" button; pressing `Enter` starts the mode.

## [0.8.0] - 2026-03-22

### Added
- **Connection overlay** — when bitcoin-tui cannot reach Bitcoin Core, the tab area is replaced by a centered overlay showing network, endpoint, data directory, auth method, and the error message; Enter/Esc/q all quit from this state
- **Shutdown bitcoind** — Tools tab now has a "Shutdown" section with a "Shutdown bitcoind & exit" option; press `[Q]` or navigate with `arrows` and press `Enter`; sends `stop` RPC command and exits the TUI

### Changed
- Cookie credentials are refreshed from disk before each poll cycle when disconnected, so a bitcoind restart (which writes a new `.cookie`) is picked up automatically without restarting bitcoin-tui
- Credentials split into a separate `RpcAuth` struct (from `RpcConfig`), protected by a `Guarded<T>` mutex wrapper; makes credential updates from the poll thread safe while other threads construct `RpcClient` instances

## [0.7.1] - 2026-03-17

### Changed
- RPC client now uses HTTP/1.1 with `Connection: close` to fix empty-response errors against Bitcoin Core
- Default RPC timeout increased from 10s to 30s to accommodate slow responses during initial startup (mempool load, IBD catch-up)
- Removed `Start height` field from peer detail overlay — `startingheight` is deprecated in Bitcoin Core v31 and will be removed in v32

### Fixed
- Improved RPC error messages: timeout and connection-closed failures are now reported distinctly instead of both showing as "Empty response from Bitcoin Core"

## [0.7.0] - 2026-03-09

### Added
- **Peer actions** - from the peer detail overlay, select Disconnect or Ban (24h) and press `Enter` to execute; result is shown in a floating overlay with a success or error message; press `Esc` to dismiss
- **Added Nodes overlay** - press `[a]` from the Peers tab to open a centered overlay listing all added nodes with connection status (● connected / ○ not connected); press `down/up-arrow` to navigate, `Enter` to remove a node, `[a]` to add a new node, `Esc` to close
- **Ban List overlay** - press `[b]` from the Peers tab to open a centered overlay listing all banned addresses with their expiry time; press `down/up-arrow` to navigate, `Enter` to unban, `Esc` to close
- **Soft-fork Tracking** - Network tab now shows a table of all consensus deployments (`getdeploymentinfo`) with name, type (buried/bip9), status, and activation height; loaded once on first visit; status colored green (active), yellow (locked\_in), cyan (started)

### Changed
- Network tab: Network Status and Node panels are now displayed side by side
- Long peer addresses (onion, i2p) in the peer detail overlay are displayed on their own line to avoid truncation; IPv4 and IPv6 addresses remain on one line

## [0.6.1] - 2026-03-04

### Added
- **Block navigation in Mempool tab** - press `down-arrow` to enter block selection mode; use `left/right` to move between blocks (newest first); press `Enter` to open the full block detail overlay (same as searching by height); press `Esc` to deselect and return to normal tab navigation

## [0.6.0] - 2026-03-04

### Added
- **Block age** - each block column in the Mempool tab now shows how long ago the block was mined (e.g. `14m ago`), fetched via the `time` field from `getblockstats`
- **Adaptive block count** - the Mempool tab now fills the available terminal width with block columns (up to 20 blocks fetched), rather than always showing 7

## [0.5.0] - 2026-03-04

### Added
- **Peer detail overlay** - in the Peers tab, navigate with `↑/↓` to select a peer and press `Enter` to open a detail overlay showing address, direction, network, user agent, protocol version, services, ping, min ping, uptime, connection type, transport, bytes sent/received, tip height, starting height, BIP-152 compact block status, and addresses processed
- Extended `PeerInfo` with additional fields from `getpeerinfo`: `conntime`, `startingheight`, `connection_type`, `transport_protocol_type`, `servicesnames`, `minping`, `bip152_hb_from`, `bip152_hb_to`, `addr_processed`

## [0.4.0] - 2026-03-03

### Added
- **Tools tab** - new fifth tab for transaction utilities
- **Broadcast Transaction** - paste a raw hex transaction and submit it via `sendrawtransaction`; press `[b]` or navigate with `↑/↓` and press `Enter`, type or paste the hex, confirm with `Enter`; the result txid is selectable and pressing `Enter` on it opens the transaction in the search overlay
- **Private Broadcast Queue** - live view of transactions pending private broadcast (`getprivatebroadcastinfo`, Bitcoin Core PR #29415); shown automatically when the queue is non-empty

## [0.3.0] - 2026-02-27

### Added
- **Transaction drill-down** - from the confirmed-tx search overlay, navigate to the block (press `enter` on the Block # row) and back (Escape returns to the transaction); full navigation history stack so Escape always unwinds one level at a time
- **Inputs overlay** - navigate to the Inputs row in a confirmed-tx overlay and press `enter` to open a dedicated sub-overlay listing all inputs with their full 64-char txids and output indices; coinbase inputs are labeled `coinbase`
- **Outputs overlay** - navigate to the Outputs row in a confirmed-tx overlay and press `enter` to open a dedicated sub-overlay listing all outputs with value (BTC) and address; addresses up to 60 chars shown in full, longer ones truncated symmetrically (`first28…last28`)
- **Faster startup** - polling is now two-phase: core data (blockchain, network, mempool, peers) is committed and rendered after 4 fast RPC calls; block stats are fetched in a second phase without blocking the initial render
- **Removed `getmininginfo`** - hash rate is now derived directly from difficulty (`difficulty × 2³² / 600`), eliminating one RPC call

### Changed
- Sub-overlays for inputs and outputs use a wider panel (84 chars) to accommodate full txids without truncation
- Confirmed-tx overlay rows are navigatable with `up arrow`/`down arrow`; pressing `enter` on Block #, Inputs, or Outputs opens the relevant view

## [0.2.1] - 2026-02-26

### Added
- Animated recent blocks: when a new block arrives, existing blocks slide right (~480 ms); driven by a dedicated 25 fps ticker thread that only runs during the animation

### Changed
- Footer last-update timestamp now shows time only (`HH:MM:SS`) instead of date and time

## [0.2.0] - 2026-02-23

### Added
- **Global txid search** - press `/` from anywhere to activate the search bar in the tab bar. Type a txid and press Enter: the app checks the mempool (`getmempoolentry`) first, then falls back to confirmed blocks (`getrawtransaction`, requires `txindex=1`). Results appear as a floating overlay panel on the Mempool tab. Mempool results show fee, fee rate, vsize, weight, ancestor/descendant counts, and time in mempool. Confirmed results show confirmation count, block hash, block age, vsize, weight, input/output counts, and total output value. The search bar is compact when idle and expands on activation; the text display scrolls to keep the cursor visible when the input overflows.
- **Contextual footer** - keybind hints update in real-time: `[Enter] search  [Esc] cancel` while typing, `[Esc] dismiss  [q] quit` while an overlay is visible, and the normal navigation hints otherwise. Escape always acts on the innermost context first (cancel input → dismiss overlay → quit).

## [0.1.0] - 2025-01-15

### Added
- Initial release
- **Dashboard** - blockchain height, difficulty, sync progress, network status, mempool summary
- **Mempool** - transaction count, virtual size, total fees, min relay fee, memory gauge, recent block fill visualization
- **Network** - connection counts, client version, protocol version, relay fee
- **Peers** - live peer table with address, network type, direction, ping, bytes sent/received, tip height
- Cookie authentication (auto-detected) with fallback to `--user`/`--password`
- Configurable refresh interval (`--refresh`)
- `--testnet`, `--regtest`, `--signet` shortcuts
- 48 unit tests for the vendored JSON implementation

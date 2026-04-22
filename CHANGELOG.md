# Changelog

All notable changes to bitcoin-tui are documented here.

## [Unreleased]

### Added
- **Mouse support** - click tabs to switch between them
- **Clickable footer bar** - footer hints are now rendered as a dedicated mouse-aware footer bar; click refresh/search/quit and tab-specific actions directly
- **Lua scripting** - load custom tabs from Lua scripts with `--tab <path.lua>`; scripts can call a configurable set of RPC methods (allowlisted with `--allow-rpc`); optional debug log via `--debuglog`; bundled example: slow-block monitor tab
- **Lua footer buttons** - Lua scripts can register clickable footer actions with `btcui_add_footer_button(label, callback)`; `btcui_show_search_button(bool)` and `btcui_show_quit_button(bool)` let scripts hide the global search and quit buttons per-tab
- **Config file support** - options can be set in a `config.toml` file (Linux: `$XDG_CONFIG_HOME/bitcoin-tui/` or `~/.config/bitcoin-tui/`; macOS: `~/Library/Application Support/bitcoin-tui/`; Windows: `%APPDATA%\bitcoin-tui\`); CLI flags override file values; path can be changed with `--config`
- CLI11 replaces hand-rolled argument parsing; adds `--help` grouping, `--config` file support, and stricter validation of unknown flags

### Changed
- FTXUI updated from v5.0.0 to v6.1.9
- Footer hints are unified across tabs: context-sensitive actions now appear in one shared footer bar instead of each tab rendering its own status text

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

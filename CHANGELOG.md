# Changelog

All notable changes to bitcoin-tui are documented here.

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

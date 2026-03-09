# Changelog

All notable changes to bitcoin-tui are documented here.

## [0.7.0] - 2026-03-09

### Added
- **Peer actions** - from the peer detail overlay, select Disconnect or Ban (24h) and press `Enter` to execute; result is shown in a floating overlay with a success or error message; press `Esc` to dismiss
- **Added Nodes overlay** - press `[a]` from the Peers tab to open a centered overlay listing all added nodes with connection status (● connected / ○ not connected); press `down/up-arrow` to navigate, `Enter` to remove a node, `[a]` to add a new node, `Esc` to close
- **Ban List overlay** - press `[b]` from the Peers tab to open a centered overlay listing all banned addresses with their expiry time; press `down/up-arrow` to navigate, `Enter` to unban, `Esc` to close

### Changed
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

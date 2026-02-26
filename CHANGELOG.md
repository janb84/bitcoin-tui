# Changelog

All notable changes to bitcoin-tui are documented here.

## [0.2.1] - 2026-02-26

### Added
- Animated recent blocks: when a new block arrives, existing blocks slide right (~480 ms); driven by a dedicated 25 fps ticker thread that only runs during the animation

### Changed
- Footer last-update timestamp now shows time only (`HH:MM:SS`) instead of date and time

## [0.2.0] - 2026-02-23

### Added
- **Global txid search** — press `/` from anywhere to activate the search bar in the tab bar. Type a txid and press Enter: the app checks the mempool (`getmempoolentry`) first, then falls back to confirmed blocks (`getrawtransaction`, requires `txindex=1`). Results appear as a floating overlay panel on the Mempool tab. Mempool results show fee, fee rate, vsize, weight, ancestor/descendant counts, and time in mempool. Confirmed results show confirmation count, block hash, block age, vsize, weight, input/output counts, and total output value. The search bar is compact when idle and expands on activation; the text display scrolls to keep the cursor visible when the input overflows.
- **Contextual footer** — keybind hints update in real-time: `[Enter] search  [Esc] cancel` while typing, `[Esc] dismiss  [q] quit` while an overlay is visible, and the normal navigation hints otherwise. Escape always acts on the innermost context first (cancel input → dismiss overlay → quit).

## [0.1.0] - 2025-01-15

### Added
- Initial release
- **Dashboard** — blockchain height, difficulty, sync progress, network status, mempool summary
- **Mempool** — transaction count, virtual size, total fees, min relay fee, memory gauge, recent block fill visualization
- **Network** — connection counts, client version, protocol version, relay fee
- **Peers** — live peer table with address, network type, direction, ping, bytes sent/received, tip height
- Cookie authentication (auto-detected) with fallback to `--user`/`--password`
- Configurable refresh interval (`--refresh`)
- `--testnet`, `--regtest`, `--signet` shortcuts
- 48 unit tests for the vendored JSON implementation

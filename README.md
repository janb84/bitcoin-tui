# Bitcoin-tui

A terminal UI for [Bitcoin Core](https://github.com/bitcoin/bitcoin) built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

Connects to a local or remote Bitcoin Core node via JSON-RPC and displays live blockchain, mempool, network, and peer data — no external libraries required beyond FTXUI.

## Screenshot

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ ₿ Bitcoin Core TUI   127.0.0.1:8332                              mainnet    │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────────────────┐
│  Dashboard  │  Mempool  │  Network  │  Peers                                │
└─────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────┐┌─────────────────────────────────────┐
│ Blockchain                           ││ Network                             │
│   Chain       : mainnet              ││   Active      : yes                 │
│   Height      : 884,231              ││   Connections : 42                  │
│   Headers     : 884,231              ││     In        : 30                  │
│   Difficulty  : 113.76 T             ││     Out       : 12                  │
│   Hash Rate   : 812.43 EH/s          ││   Client      : /Satoshi:27.0.0/   │
│   Sync        : ████████████ 100%    ││   Protocol    : 70016               │
│   IBD         : no                   ││   Relay fee   : 1.0 sat/vB          │
│   Pruned      : no                   │└─────────────────────────────────────┘
└──────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────────────────┐
│ Mempool                                                                     │
│   Transactions: 312,847                                                     │
│   Size        : 487.2 MB                                                    │
│   Total fee   : 2.3841 BTC                                                  │
│   Min fee     : 1.5 sat/vB                                                  │
│   Memory      : ████████░░░░░░░░░░░░  487.2 MB / 300.0 MB                  │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────────────────┐
│ ● CONNECTED  Last update: 2025-01-15 14:32:07    ↻ every 5s  [Tab] [q]     │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Peers tab:**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ ID   Address                   Net  I/O Ping ms  Recv       Sent    Height  │
├─────────────────────────────────────────────────────────────────────────────┤
│ 0    144.76.31.85:8333         ipv4 out 14.2     98.4 MB   12.1 MB  884,231 │
│ 1    [2001:db8::1]:8333        ipv6 out 31.7     54.2 MB    8.7 MB  884,230 │
│ 2    203.0.113.42:8333         ipv4 in  22.9    210.5 MB   45.3 MB  884,229 │
│ 3    example3xample.onion:8333 onio out 88.1     31.2 MB    5.1 MB  884,231 │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Features

- **Dashboard** — blockchain height, difficulty, sync progress, network status, and mempool summary at a glance
- **Mempool** — transaction count, virtual size, total fees, min relay fee, and memory usage gauge
- **Network** — connection counts (inbound/outbound), client version, protocol version, relay fee
- **Peers** — live peer table with address, network type, direction, ping, bytes sent/received, and tip height
- Background polling thread — non-blocking UI with configurable refresh interval
- No external dependencies beyond FTXUI (JSON parsing and HTTP handled in-tree)
- 48 unit tests for the JSON implementation (Catch2 v3)

## Requirements

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.14+
- Internet access at configure time (CMake fetches FTXUI v5.0.0 and Catch2 v3.7.1 via FetchContent)
- Bitcoin Core node with RPC enabled

## Build

```sh
cmake -B build
cmake --build build -j$(nproc)
```

Binary is output to `build/bin/bitcoin-tui`.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

Tests cover the vendored JSON implementation: parsing, serialization, type queries, accessor methods, error handling, and real Bitcoin Core RPC response shapes.

## Usage

```
bitcoin-tui [options]

Options:
  -h, --host <host>      RPC host         (default: 127.0.0.1)
  -p, --port <port>      RPC port         (default: 8332)
  -u, --user <user>      RPC username
  -P, --password <pass>  RPC password
  -r, --refresh <secs>   Refresh interval (default: 5)
      --testnet          Use testnet port (18332)
      --regtest          Use regtest port (18443)
      --signet           Use signet  port (38332)

Keyboard:
  Tab / Left / Right     Switch tabs
  q / Escape             Quit
```

### Examples

```sh
# Mainnet with credentials
./build/bin/bitcoin-tui -u alice -P hunter2

# Testnet with faster refresh
./build/bin/bitcoin-tui --testnet -u alice -P hunter2 -r 2

# Remote node
./build/bin/bitcoin-tui --host 192.168.1.10 -u alice -P hunter2
```

## Bitcoin Core configuration

Ensure `bitcoin.conf` has RPC enabled:

```ini
server=1
rpcuser=alice
rpcpassword=hunter2
rpcallowip=127.0.0.1
rpcbind=127.0.0.1
```

## License

MIT

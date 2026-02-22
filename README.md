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
│   Hash Rate   : 812.43 EH/s          ││   Client      : /Satoshi:27.0.0/    │
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
│   Memory      : ████████░░░░░░░░░░░░  487.2 MB / 300.0 MB                   │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────────────────┐
│ ● CONNECTED  Last update: 2025-01-15 14:32:07    ↻ every 5s  [Tab] [q]      │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Mempool tab:**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ Mempool                                                                     │
│   Transactions    : 312,847                                                 │
│   Virtual size    : 156.3 MB                                                │
│   Total fees      : 1.23450000 BTC                                          │
│   Min relay fee   : 1.0 sat/vB                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│   Memory usage                                                              │
│   ████████████████████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░    │
│   Used : 156.3 MB  /  Max : 300.0 MB                                        │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────────────────┐
│ Recent Blocks                                                               │
│  ██████████ ░░░░░░░░░░ ░░░░░░░░░░ ██████████ ░░░░░░░░░░ ░░░░░░░░░░          │
│  ██████████ ██████████ ░░░░░░░░░░ ██████████ ██████████ ██████████          │
│  ██████████ ██████████ ░░░░░░░░░░ ██████████ ██████████ ██████████          │
│  ██████████ ██████████ ░░░░░░░░░░ ██████████ ██████████ ██████████          │
│  ██████████ ██████████ ██████████ ██████████ ██████████ ██████████          │
│  ██████████ ██████████ ██████████ ██████████ ██████████ ██████████          │
│   884,231    884,230    884,229    884,228    884,227    884,226            │
│  3,421 tx   2,891 tx    412 tx   3,104 tx   2,543 tx   2,987 tx             │
│   1.5 MB     1.2 MB    0.2 MB     1.4 MB     1.1 MB     1.3 MB              │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Peers tab:**

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│ ID   Address                       Net  I/O Ping ms      Recv      Sent   Height  │
├───────────────────────────────────────────────────────────────────────────────────┤
│ 0    144.76.31.85:8333             ipv4 out    14.2   98.4 MB  12.1 MB  884,231   │
│ 1    [2001:db8::1]:8333            ipv6 out    31.7   54.2 MB   8.7 MB  884,230   │
│ 2    203.0.113.42:8333             ipv4 in     22.9  210.5 MB  45.3 MB  884,229.  │
│ 3    vww6ybal4bd7szmgncyruucpg…    onio out    88.1   31.2 MB   5.1 MB  884,231   │
└───────────────────────────────────────────────────────────────────────────────────┘
```

## Features

- **Dashboard** — blockchain height, difficulty, sync progress, network status, and mempool summary at a glance
- **Mempool** — transaction count, virtual size, total fees, min relay fee, memory usage gauge, and recent block fill visualization (newest first, colored green/yellow/orange by weight)
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

Connection:
  -h, --host <host>      RPC host             (default: 127.0.0.1)
  -p, --port <port>      RPC port             (default: 8332)

Authentication (cookie auth is used by default):
  -c, --cookie <path>    Path to .cookie file (auto-detected if omitted)
  -d, --datadir <path>   Bitcoin data directory for cookie lookup
  -u, --user <user>      RPC username         (disables cookie auth)
  -P, --password <pass>  RPC password         (disables cookie auth)

Network:
      --testnet          Use testnet3 port (18332) and cookie subdir
      --regtest          Use regtest  port (18443) and cookie subdir
      --signet           Use signet   port (38332) and cookie subdir

Display:
  -r, --refresh <secs>   Refresh interval     (default: 5)
  -v, --version          Print version and exit

Keyboard:
  Tab / Left / Right     Switch tabs
  q / Escape             Quit
```

### Examples

```sh
# Mainnet — auto-detects ~/.bitcoin/.cookie (or ~/Library/Application Support/Bitcoin/.cookie on macOS)
./build/bin/bitcoin-tui

# Testnet — uses testnet3/.cookie automatically
./build/bin/bitcoin-tui --testnet

# Custom data directory
./build/bin/bitcoin-tui --datadir /mnt/bitcoin

# Explicit cookie file
./build/bin/bitcoin-tui --cookie /var/lib/bitcoind/.cookie

# Explicit credentials (rpcuser/rpcpassword style)
./build/bin/bitcoin-tui -u alice -P hunter2

# Remote node with faster refresh
./build/bin/bitcoin-tui --host 192.168.1.10 -u alice -P hunter2 -r 2
```

## Bitcoin Core configuration

Cookie authentication is the default. Just enable the RPC server in `bitcoin.conf`:

```ini
server=1
```

Bitcoin Core writes `.cookie` to the data directory on startup. `bitcoin-tui` reads it automatically — no credentials needed.

For remote access or `rpcuser`/`rpcpassword`-style auth, add:

```ini
server=1
rpcallowip=127.0.0.1
rpcbind=127.0.0.1
rpcuser=alice
rpcpassword=hunter2
```

Then pass `-u alice -P hunter2` on the command line.

## License

MIT

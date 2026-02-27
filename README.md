# Bitcoin-tui

A terminal UI for a [Bitcoin node](https://github.com/bitcoin/bitcoin) built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

Connects to a local or remote Bitcoin Core node via JSON-RPC and displays live blockchain, mempool, network, and peer data - no external libraries required beyond FTXUI.

## Screenshot


<img width="863" height="671" alt="Screenshot dashboard" src="https://github.com/user-attachments/assets/95ebcd57-b1df-41c8-80bf-f555755dbae8" />


**Mempool tab:**

<img width="879" height="674" alt="Screenshot mempool" src="https://github.com/user-attachments/assets/d8cf47f0-bd12-403a-9534-602bd12ce139" />


## Features

- **Dashboard** - blockchain height, difficulty, sync progress, network status, and mempool summary at a glance
- **Mempool** - transaction count, virtual size, total fees, min relay fee, memory usage gauge, and animated recent block fill visualization (newest first, colored green/yellow/orange by weight - blocks slide right when a new block arrives)
- **Search** - press `/` to search mempool or confirmed transactions (txid); drill into blocks, inputs, and outputs (`txindex=1` required for confirmed lookups)
- **Network** - connection counts (inbound/outbound), client version, protocol version, relay fee
- **Peers** - live peer table with address, network type, direction, ping, bytes sent/received, and tip height
- Background polling thread - non-blocking UI with configurable refresh interval
- No external dependencies beyond FTXUI (JSON parsing and HTTP handled in-tree)

## Requirements

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.14+
- Internet access at configure time (CMake fetches FTXUI v5.0.0 and Catch2 v3.7.1 via FetchContent)
- Bitcoin (Core) node with RPC enabled

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

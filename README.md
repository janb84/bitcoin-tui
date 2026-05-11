# Bitcoin-tui

A terminal UI for a [Bitcoin node](https://github.com/bitcoin/bitcoin) built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

Connects to a local or remote Bitcoin Core node via JSON-RPC and displays live blockchain, mempool, network, and peer data — and lets you broadcast transactions directly from the terminal. No external libraries required beyond FTXUI.

## Screenshot

**Dashboard tab:**

<img width="941" height="694" alt="Screenshot of Dashboard tab" src="https://github.com/user-attachments/assets/b411bbb6-e64f-4581-812a-f91fef2e6532" />

**Mempool tab:**

<img width="933" height="691" alt="Screenshot of Mempool tab" src="https://github.com/user-attachments/assets/58496173-b7c8-495e-b591-147d8ae8f578" />

**Block search overlay:**

<img width="938" height="695" alt="Screenshot of Block search overlay" src="https://github.com/user-attachments/assets/f8d1d57e-e93d-4dbd-bb6d-a75e429cd100" />


## Features

- **Dashboard** - blockchain height, difficulty, sync progress, network status, and mempool summary at a glance
- **Mempool** - transaction count, virtual size, total fees, min relay fee, memory usage gauge, and animated recent block fill visualization (newest first, colored green/yellow/orange by weight - blocks slide right when a new block arrives; block age shown per column; number of columns adapts to terminal width)
- **Search** - press `/` to search mempool or confirmed transactions (txid); drill into blocks, inputs, and outputs (`txindex=1` required for confirmed lookups)
- **Network** - connection counts (inbound/outbound), client version, protocol version, relay fee; soft-fork tracking table showing all consensus deployments with status and activation height (`getdeploymentinfo`, loaded on first visit)
- **Peers** - live peer table with address, network type, direction, ping, bytes sent/received, and tip height; navigate with `down/up-arrow` and press `Enter` to open a detail overlay for any peer; disconnect or ban (24h) from the detail overlay; press `[a]` to view/manage added nodes, `[b]` to view/manage the ban list
- **Tools** - broadcast raw transactions via `sendrawtransaction`; live private broadcast queue (Bitcoin Core PR #29415, shown when non-empty); shutdown Bitcoin Core node and exit with `[Q]`

- Background polling thread - non-blocking UI with configurable refresh interval
- No external dependencies beyond FTXUI (JSON parsing and HTTP handled in-tree)

## Requirements

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.22+
- Internet access at configure time (CMake fetches FTXUI v6.1.9 and Catch2 v3.7.1 via FetchContent)
- Bitcoin (Core) node with RPC enabled
- [Cap'n Proto](https://capnproto.org/) >= 1.0 (`brew install capnp` / `apt install capnproto`) for the IPC transport. Required by default; pass `-DWITH_IPC=OFF` at configure time to skip it (the TUI then falls back to time-based polling and the `test_ipc` suite is omitted). IPC is forced off on Windows pending [bitcoin-core/libmultiprocess#231](https://github.com/bitcoin-core/libmultiprocess/pull/231).

## Build

```sh
cmake -B build
cmake --build build -j$(nproc)

# Build without IPC support (no Cap'n Proto required)
cmake -B build -DWITH_IPC=OFF
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
      --testnet4         Use testnet4 port (48332) and cookie subdir
      --regtest          Use regtest  port (18443) and cookie subdir
      --signet           Use signet   port (38332) and cookie subdir

Launch:
      --bitcoind <path>  Path to bitcoind binary (default: found via PATH)

Node (IPC):
      --ipcconnect <path>  Connect to bitcoin-node via a Cap'n Proto IPC
                           socket. Defaults to <datadir>/<net>/node.sock if
                           present; pass an explicit path to override, or
                           'off' to disable. When connected, the poll loop
                           wakes immediately on new tips via
                           Mining.waitTipChanged instead of waiting for
                           --refresh, and an `IPC` badge appears in the
                           footer.

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

## IPC (Cap'n Proto, optional)

When `bitcoin-node` is launched with `-ipcbind=unix`, it exposes a typed
Cap'n Proto socket at `<datadir>/<network>/node.sock`. `bitcoin-tui` can
attach to that socket in addition to JSON-RPC and use it to wake the poll
loop the moment a new tip arrives, rather than waiting up to `--refresh`
seconds. JSON-RPC over HTTP is still used for everything else.

```sh
# bitcoin-node side (testnet4)
bitcoin-node -testnet4 -ipcbind=unix -server=1

# bitcoin-tui side: socket auto-detected from --datadir + network
./build/bin/bitcoin-tui --testnet4

# Or point at the socket explicitly
./build/bin/bitcoin-tui --ipcconnect ~/Library/Application\ Support/Bitcoin/testnet4/node.sock

# Disable even if the socket exists
./build/bin/bitcoin-tui --ipcconnect off
```

The footer shows an `IPC` badge when the typed transport is active. v31.0
of Bitcoin Core only exposes the `Mining` interface over IPC, which is all
`bitcoin-tui` needs for tip-change notifications. v32 will additionally
expose JSON-RPC over the same socket via
[bitcoin/bitcoin#32297](https://github.com/bitcoin/bitcoin/pull/32297).

## License

MIT

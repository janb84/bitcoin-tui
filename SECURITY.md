# Security Policy

`bitcoin-tui` is a read-mostly terminal client for Bitcoin Core. It holds no
private keys and has no wallet of its own, but it does handle RPC credentials
and can perform node-affecting actions (broadcast transactions, ban peers, stop
the node). Security reports are welcome and taken seriously.

## Supported versions

Only the latest release line receives security fixes. Older tags are not
patched — upgrade instead.

| Version | Supported |
| ------- | --------- |
| 0.9.x   |    yes    |
| 0.8.x   |    yes    |
| < 0.8   |    no     |

## Reporting a vulnerability

**Please do not open a public issue for security problems.**

Report privately through one of these channels:

1. **GitHub private vulnerability reporting** (preferred) -
   [open a draft advisory](https://github.com/janb84/bitcoin-tui/security/advisories/new)
   on the repository's Security tab.
2. **Email** - bitcoin-tui.vista651@passmail.com. This is a forwarding alias
   meant for first contact; it reaches the maintainer, who will move the
   conversation to a more direct or encrypted channel if the report warrants
   one. Please keep sensitive detail out of the subject line.

Please include:

- affected version (`bitcoin-tui --version`) and platform/compiler,
- Bitcoin Core version and network (mainnet/testnet/signet/regtest),
- a description of the issue and its impact,
- reproduction steps, a minimal proof of concept, or a crashing input if you
  have one,
- whether you would like to be credited, and under what name.

### What to expect

| Stage                 | Target                                    |
| --------------------- | ----------------------------------------- |
| Acknowledgement       | within 5 business days                    |
| Initial assessment    | within 10 business days                   |
| Fix or mitigation     | depends on severity; coordinated with you |
| Public disclosure     | after a fix ships, or 90 days by default  |

This is a volunteer-maintained project — timelines are goals, not guarantees.
We will keep you updated if something takes longer. Reporters are credited in
the advisory and `CHANGELOG.md` unless they ask otherwise.

## Scope

### In scope

Issues in this repository's code, including:

- **Credential handling** — leaking RPC cookies, `rpcuser`/`rpcpassword`, or
  `bitcoin.conf` contents into logs, debug files (`--debug-file`), the terminal,
  crash output, or files with unsafe permissions.
- **Vendored JSON parser** (`src/json.hpp`) — memory-safety bugs (buffer
  overreads, unbounded recursion, allocation blowups) triggerable by an RPC
  response.
- **RPC transport** (`src/rpc_client.*`) — HTTP response parsing bugs,
  request smuggling, or handling of hostile/oversized node replies.
- **Lua host** (`src/tabs/luatab.*`, `lua/`) — escapes from the RPC allowlist
  (`--allow-rpc` / per-tab `allow_rpc`), a Lua tab reaching credentials or RPC
  methods it was not granted, or path traversal in tab loading.
- **Config and path resolution** (`src/paths.hpp`, `config.toml` parsing) —
  unsafe file handling, privilege issues when run under `sudo`.
- **Memory safety anywhere** — use-after-free, data races, or overflows,
  particularly across the polling/UI/Lua threads.

### Out of scope

- Vulnerabilities in **Bitcoin Core** itself — report those to the
  [Bitcoin Core security process](https://bitcoincore.org/en/contact/).
- Vulnerabilities in third-party dependencies (FTXUI, Lua, LuaBridge3, Abseil,
  RE2, CLI11, Catch2) — report upstream; we will pick up the fix. Do tell us if
  `bitcoin-tui` is exploitable through one of them.
- Attacks requiring an already-compromised local machine or an attacker who can
  already read the Bitcoin data directory (they have the cookie file anyway).
- Running `bitcoin-tui` against an RPC endpoint you do not trust. The client
  assumes the node is trusted; a malicious node can feed you bogus chain data by
  design. Memory-safety bugs reachable from such data *are* in scope.
- Exposing the RPC port to the internet, or a `bitcoin.conf` with weak
  credentials — that is node configuration, not a client bug.
- Custom third-party Lua tabs the user chose to install. A Lua tab runs with the
  privileges the user grants it; **escaping** the granted allowlist is in scope,
  **abusing** a granted permission is not.
- Missing hardening flags, or automated-scanner output with no demonstrated
  impact.

## Security model, in brief

Users should know what this tool does and does not protect:

- **No wallet, no keys.** `bitcoin-tui` never handles private keys or seeds.
  The Tools tab broadcasts *already-signed* raw transactions supplied by the
  user.
- **Credentials.** Cookie auth is the default and preferred path; the cookie is
  read from the node's data directory. Passing `-u`/`-P` puts credentials in
  your shell history and process list (`ps`), visible to other local users —
  prefer cookie auth or `config.toml` with restrictive permissions.
- **Plaintext RPC.** JSON-RPC is sent over unencrypted HTTP. Use it over
  loopback, or tunnel remote connections through SSH or a VPN — never across
  the open internet.
- **Destructive actions.** Peer ban/disconnect and node shutdown are reachable
  from the UI. Anyone with access to your terminal has that reach.
- **Lua tabs execute code.** A Lua tab is arbitrary code running as your user.
  Only install tabs you trust, and grant the narrowest `allow_rpc` set that
  works.

## Hardening the build

Sanitizer builds are used during development and are welcome in reports:

```sh
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j$(nproc)
```

Reproducible builds are available under `contrib/guix/` — see that directory
for verifying that a released binary matches this source.

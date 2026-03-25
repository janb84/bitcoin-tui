# Bitcoin-tui Nix packages

These [Nix](https://nixos.org/learn/) packages provide a way to build bitcoin-tui binary packages for various Linux distributions:

- **bitcoin-tui** - This package builds bitcoin-tui from source and provides a package for the [NixOS](https://nixos.org/) Linux distribution. However, its primary purpose is to serve as a source binary package for other Linux distributions.
- **bitcoin-tui-tar** - This package takes the former **bitcoin-tui** package, removes the Nix store paths from the binary, and repackages it as a tgz file. Hence, the binary in this package can execute on non-NixOS distros, as long as the required libraries are provided.
- **bitcoin-tui-debian** - This package takes the former **bitcoin-tui-tar** package and repackages it as a Debian package.
- **bitcoin-tui-fedora** - This package takes the former **bitcoin-tui-tar** package and repackages it as a Fedora (RedHat) package.

## Build

To build these packages, you need to install the [Nix](https://nixos.org/learn/) package manager, and it needs to be configured with *flakes* support. The simplest way to obtain Nix in this configuration is to use [nix-portable](https://github.com/DavHau/nix-portable).

```sh
curl -L https://github.com/DavHau/nix-portable/releases/latest/download/nix-portable-$(uname -m) > ./nix-portable

chmod +x ./nix-portable
```

Next, use *nix-portable* to enter a BASH shell containing the Nix package manager:


```sh
./nix-portable nix shell nixpkgs#{bashInteractive,nix} -c bash
```

Within this shell, you can build any Nix package. The build output is a symlink named *result* which points to a directory within the Nix store, which in turn contains the built package:

### Build the Debian package

```sh
nix build github:janb84/bitcoin-tui#bitcoin-tui-debian
cp ./result/* ~/
exit
```

### Build the Fedora/RedHat package

```sh
nix build github:janb84/bitcoin-tui#bitcoin-tui-fedora
cp ./result/* ~/
exit
```

### Build from a local clone

The examples above illustrate building from the main branch of the repo on GitHub, but you can also build from within a local clone of the repo with a slight change to the syntax:

```sh
nix build .#bitcoin-tui-debian
```

## Maintenance

These awesome reproducible builds provided by Nix don't come for free, son. On-going minimal maintenance is required to keep this ship sailing.

### Update Nixpkgs

The [Nixpkgs](https://github.com/NixOS/nixpkgs) repository needs to be updated periodically to keep the tool-chain up-to-date. 1-2 updates per year should be good enough. To do this, `cd` into a clone of this repo and execute an update as follows:

```sh
nix flake update
```

The command above will update *flake.lock* to reflect the latest snapshot of Nixpkgs. Naturally, this change then needs to be committed to this git repo.

### Update FTXUI and Catch2 

Whenever the FTXUI and/or Catch2 versions are updated in `CMakeLists.txt`, the versions and checksums also need to be updated in *nix/bitcoin-tui.nix*.

For example, lets say FTXUI is updated to v6.0.0. To make this change in *nix/bitcoin-tui.nix* make an edit like so:

```nix
  ftxui = fetchFromGitHub {
    owner = "ArthurSonzogni";
    repo = "FTXUI";
    tag = "v6.0.0";            # Change the version here.
    sha256 = "";               # Clear out the checksum here.
  };
```

Next, attempt to build *bitcoin-tui*. It will fail, but the error will contain the *actual* checksum:

```sh
nix build .#bitcoin-tui
```

Take that *actual* checksum and enter it into the *sha256* variable. The build should then succeed.

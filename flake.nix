{
  description = "Nix packages for bitcoin-tui, a terminal UI for bitcoin.";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/master";
  };

  outputs = { self, nixpkgs }: let
    forAllSystems = func: nixpkgs.lib.genAttrs [
      "x86_64-linux"
      "aarch64-linux"
    ] (system:
      func (
        import nixpkgs {
          inherit system;
        }
      )
      system
    );
  in {
    packages = forAllSystems (pkgs: system: {
      bitcoin-tui = pkgs.callPackage ./nix/bitcoin-tui.nix {};
    });
  };
}

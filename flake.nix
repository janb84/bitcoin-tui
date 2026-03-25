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

      bitcoin-tui-tar = pkgs.callPackage ./nix/bitcoin-tui-tar.nix {
        inherit (self.packages."${system}") bitcoin-tui;
        inherit system;
      };

      bitcoin-tui-debian = pkgs.callPackage ./nix/bitcoin-tui-debian.nix {
        inherit (self.packages."${system}") bitcoin-tui-tar;
      };

      bitcoin-tui-fedora = pkgs.callPackage ./nix/bitcoin-tui-fedora.nix {
        inherit (self.packages."${system}") bitcoin-tui-tar;
      };
    });
  };
}

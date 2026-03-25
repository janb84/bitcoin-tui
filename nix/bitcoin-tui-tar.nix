{ stdenvNoCC
, lib
, system
, patchelf
, gnutar
, bitcoin-tui
}: let
  arch = {
    "x86_64-linux" = "amd64";
    "aarch64-linux" = "aarch64";
  }."${system}";

  ldArch = {
    "x86_64-linux" = "linux-x86-64";
    "aarch64-linux" = "linux-aarch64";
  }."${system}";
in stdenvNoCC.mkDerivation {
  pname = "bitcoin-tui-${bitcoin-tui.version}-${arch}.tar.gz";
  version = bitcoin-tui.version;
  src = bitcoin-tui;
  nativeBuildInputs = [ gnutar patchelf ];

  buildPhase = ''
    lib_base_dir=/lib/${system}-gnu

    mkdir -p ./build/usr/bin
    cp ./bin/bitcoin-tui ./build/usr/bin/

    patchelf --set-interpreter "/lib64/ld-${ldArch}.so.2" ./build/usr/bin/bitcoin-tui
    patchelf --set-rpath $lib_base_dir ./build/usr/bin/bitcoin-tui
  '';

  installPhase = ''
    archive_dir=$(mktemp -d)
    content_dir=$archive_dir/bitcoin-tui-${bitcoin-tui.version}

    mkdir $content_dir
    cp -r ./build/. $content_dir/
    cd $archive_dir
    tar -czf $out ./
  '';

  passthru = {
    inherit system;
    inherit arch;
    inherit ldArch;
  };

  meta = {
    description = "A terminal UI for bitcoin";
    longDescription = "A TGZ binary package with Nix store paths removed";
    homepage = "https://github.com/janb84/bitcoin-tui";
    license = lib.licenses.mit;
    maintainers = with lib.maintainers; [ emmanuelrosa ];
    platforms = [
      "x86_64-linux"
      "aarch64-linux"
    ];
  };
}

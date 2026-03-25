{ stdenvNoCC
, lib
, dpkg
, bitcoin-tui-tar
}: let
  controlFile = builtins.toFile "control" ''
    Package: bitcoin-tui
    Version: ${bitcoin-tui-tar.version}
    Maintainer: Emmanuel Rosa <emmanuelrosa@protonmail.com>
    Depends: libstdc++6, libc6, libgcc-s1 
    Architecture: ${bitcoin-tui-tar.passthru.arch}
    Homepage: ${bitcoin-tui-tar.meta.homepage}
    Description: ${bitcoin-tui-tar.meta.description}
  '';
in stdenvNoCC.mkDerivation rec {
  name = "bitcoin-tui_${bitcoin-tui-tar.version}-1_${bitcoin-tui-tar.passthru.arch}.deb";
  version = bitcoin-tui-tar.version;
  src = bitcoin-tui-tar;
  nativeBuildInputs = [ dpkg ];

  buildPhase = ''
    mkdir DEBIAN
    cp ${controlFile} ./DEBIAN/control
    dpkg-deb --build ./ package.deb
  '';

  installPhase = ''
    mkdir $out
    cp package.deb $out/${name}
  '';

  meta = bitcoin-tui-tar.meta // {
    longDescription = "A binary package for Debian Linux";
  };
}

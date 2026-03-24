{ stdenv
, lib
, runCommand
, gnused
, cmake
, fetchFromGitHub
}: let
  ftxui = fetchFromGitHub {
    owner = "ArthurSonzogni";
    repo = "FTXUI";
    tag = "v5.0.0";
    sha256 = "sha256-IF6G4wwQDksjK8nJxxAnxuCw2z2qvggCmRJ2rbg00+E=";
  };
  
  catch2 = fetchFromGitHub {
    owner = "catchorg";
    repo = "Catch2";
    tag = "v3.7.1";
    sha256 = "sha256-Zt53Qtry99RAheeh7V24Csg/aMW25DT/3CN/h+BaeoM=";
  };
in stdenv.mkDerivation rec {
  pname = "bitcoin-tui";

  version = lib.fileContents(runCommand "extract-bitcoin-tui-version" {
    nativeBuildInputs = [ gnused ];
  } ''
    sed -n -e 's/.*bitcoin-tui[[:space:]]VERSION[[:space:]]\+\([0-9]\+\(\.[0-9]\+\)*\).*/\1/p' < ${src}/CMakeLists.txt > $out
  '');

  src = ../.;

  nativeBuildInputs = [ cmake ];

  preConfigure = ''
    export FTXUI_SRC=$(mktemp -d)
    export CATCH2_SRC=$(mktemp -d)
    cp -r ${ftxui}/. $FTXUI_SRC/
    cp -r ${catch2}/. $CATCH2_SRC/
  '';

  cmakeFlags = [
    "-DFTXUI_SRC=$(FTXUI_SRC)"
    "-DCATCH2_SRC=$(CATCH2_SRC)"
    "-DFTXUI_ENABLE_INSTALL=OFF"
  ];

  installPhase = ''
    mkdir -p $out/bin
    cp ./bin/bitcoin-tui $out/bin/
  '';

  meta = {
    description = "A terminal UI for bitcoin";
    homepage = "https://github.com/janb84/bitcoin-tui";
    mainProgram = "bitcoin-tui";
    license = lib.licenses.mit;
    maintainers = with lib.maintainers; [ emmanuelrosa ];
    platforms = [
      "x86_64-linux"
      "aarch64-linux"
    ];
  };
}

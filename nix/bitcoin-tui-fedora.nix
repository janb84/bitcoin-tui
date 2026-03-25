{ stdenvNoCC
, lib
, rpm
, bitcoin-tui-tar
}: let
  arch = {
    "x86_64-linux" = "x86_64";
    "aarch64-linux" = "aarch64";
  }."${bitcoin-tui-tar.passthru.system}";

  specFile = builtins.toFile "bitcoin-tui.spec" ''
    Name:           bitcoin-tui
    Version:        ${bitcoin-tui-tar.version}
    Release:        1%{?dist}  
    Summary:        ${bitcoin-tui-tar.meta.description}
     
    License:        MIT  
    URL:            ${bitcoin-tui-tar.meta.homepage}
    Source0:        %{name}-%{version}.tar.gz  
     
    Requires:       libstdc++
    Requires:       glibc
    Requires:       libgcc
     
    %description  
    ${bitcoin-tui-tar.meta.description}
     
    %prep  
    %setup -q   # Unpacks the source tarball into BUILD/%{name}-%{version}  
     
    %build  
    # No build step needed for a bash script (skip if not compiling)  
     
    %install  
    # Create directories in BUILDROOT (mimic target system paths)  
    mkdir -p %{buildroot}/usr/bin  
     
    # Install the script to /usr/bin (via BUILDROOT)  
    install -m 0755 usr/bin/%{name} %{buildroot}/usr/bin/%{name}  
     
    %files  
    /usr/bin/%{name}
     
    %changelog  
    * Wed Mar 25 2026 Emmanuel Rosa <emmanuelrosa@protonmail.com> 1.0-1  
    - Initial package release of bitcoin-tui
  '';

  rpmMacros = builtins.toFile "rpmmacros" ''
    %_tmppath @TEMPDIR@
  '';
in stdenvNoCC.mkDerivation rec {
  name = "bitcoin-tui_${bitcoin-tui-tar.version}-${bitcoin-tui-tar.version}-1-${arch}.rpm";
  src = bitcoin-tui-tar;
  version = bitcoin-tui-tar.version;
  nativeBuildInputs = [ rpm ];

  buildPhase = ''
    export TEMPDIR="$(mktemp -d)"
    export HOME=$(mktemp -d)
    export DBPATH=$(mktemp -d)

    mkdir -p $HOME/rpmbuild/SPECS
    mkdir -p $HOME/rpmbuild/SOURCES
    mkdir -p $HOME/rpmbuild/BUILD/bitcoin-tui-${bitcoin-tui-tar.version}-build

    cp ${specFile} $HOME/rpmbuild/SPECS/bitcoin-tui.spec
    cp ${rpmMacros} $HOME/.rpmmacros
    cp ${bitcoin-tui-tar} $HOME/rpmbuild/SOURCES/bitcoin-tui-${bitcoin-tui-tar.version}.tar.gz
    substituteInPlace $HOME/.rpmmacros --subst-var TEMPDIR 

    cd $HOME/rpmbuild/SPECS
    rpm --initdb --dbpath $DBPATH
    rpmbuild -v --dbpath $DBPATH -bb bitcoin-tui.spec
  '';

  installPhase = ''
    mkdir $out
    cp $HOME/rpmbuild/RPMS/${arch}/bitcoin-tui-${bitcoin-tui-tar.version}-1.${arch}.rpm $out/
  '';

  meta = bitcoin-tui-tar.meta // {
    longDescription = "A binary package for Fedora Linux";
  };
}

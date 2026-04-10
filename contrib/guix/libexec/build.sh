#!/usr/bin/env bash
# Copyright (c) 2026-present Bitcoin-Tui developers
# Copyright (c) 2019-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
export LC_ALL=C
set -e -o pipefail

# Environment variables for determinism
export TAR_OPTIONS="--no-same-owner --owner=0 --group=0 --numeric-owner --mtime='@${SOURCE_DATE_EPOCH}' --sort=name"
export TZ=UTC

# Set umask before any files are created.
umask 0022

if [ -n "$V" ]; then
    set -vx
    export VERBOSE="$V"
fi

# Check that required environment variables are set
cat << EOF
Required environment variables as seen inside the container:
    DIST_ARCHIVE_BASE: ${DIST_ARCHIVE_BASE:?not set}
    DISTNAME: ${DISTNAME:?not set}
    HOST: ${HOST:?not set}
    SOURCE_DATE_EPOCH: ${SOURCE_DATE_EPOCH:?not set}
    JOBS: ${JOBS:?not set}
    DISTSRC: ${DISTSRC:?not set}
    OUTDIR: ${OUTDIR:?not set}
    SOURCES_PATH: ${SOURCES_PATH:?not set}
EOF

ACTUAL_OUTDIR="${OUTDIR}"
OUTDIR="${DISTSRC}/output"

#####################
# Environment Setup #
#####################

# Given a package name and an output name, return the path of that output in our
# current guix environment
store_path() {
    grep --extended-regexp "/[^-]{32}-${1}-[^-]+${2:+-${2}}" "${GUIX_ENVIRONMENT}/manifest" \
        | head --lines=1 \
        | sed --expression='s|\x29*$||' \
              --expression='s|^[[:space:]]*"||' \
              --expression='s|"[[:space:]]*$||'
}

# Set environment variables to point the NATIVE toolchain to the right
# includes/libs
NATIVE_GCC="$(store_path gcc-toolchain)"

# Clear env vars that gcc-toolchain sets for the native arch — they would
# contaminate cross-compilation by pointing the cross-compiler at native
# headers and libraries.
unset LIBRARY_PATH
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset OBJCPLUS_INCLUDE_PATH

case "$HOST" in
    *darwin*)
        # Don't set LIBRARY_PATH — lld must find libs exclusively from the SDK sysroot.
        ;;
    *mingw*) export LIBRARY_PATH="${NATIVE_GCC}/lib" ;;
    *)
        NATIVE_GCC_STATIC="$(store_path gcc-toolchain static)"
        export LIBRARY_PATH="${NATIVE_GCC}/lib:${NATIVE_GCC_STATIC}/lib"
        ;;
esac

# Set environment variables to point the CROSS toolchain to the right
# includes/libs for $HOST
case "$HOST" in
    *mingw*)
        # Determine output paths to use in CROSS_* environment variables
        CROSS_GLIBC="$(store_path "mingw-w64-x86_64-winpthreads")"
        CROSS_GCC="$(store_path "gcc-cross-${HOST}")"
        CROSS_GCC_LIB_STORE="$(store_path "gcc-cross-${HOST}" lib)"
        CROSS_GCC_LIBS=( "${CROSS_GCC_LIB_STORE}/lib/gcc/${HOST}"/* ) # This expands to an array of directories...
        CROSS_GCC_LIB="${CROSS_GCC_LIBS[0]}" # ...we just want the first one (there should only be one)

        # The search path ordering is generally:
        #    1. gcc-related search paths
        #    2. libc-related search paths
        #    2. kernel-header-related search paths (not applicable to mingw-w64 hosts)
        export CROSS_C_INCLUDE_PATH="${CROSS_GCC_LIB}/include:${CROSS_GCC_LIB}/include-fixed:${CROSS_GLIBC}/include"
        export CROSS_CPLUS_INCLUDE_PATH="${CROSS_GCC}/include/c++:${CROSS_GCC}/include/c++/${HOST}:${CROSS_GCC}/include/c++/backward:${CROSS_C_INCLUDE_PATH}"
        export CROSS_LIBRARY_PATH="${CROSS_GCC_LIB_STORE}/lib:${CROSS_GCC_LIB}:${CROSS_GLIBC}/lib"
        ;;
    *darwin*)
        # The CROSS toolchain for darwin uses the SDK and ignores environment variables.
        # See depends/hosts/darwin.mk for more details.
        ;;
    *linux*)
        CROSS_GLIBC="$(store_path "glibc-cross-${HOST}")"
        CROSS_GLIBC_STATIC="$(store_path "glibc-cross-${HOST}" static)"
        CROSS_KERNEL="$(store_path "linux-libre-headers-cross-${HOST}")"
        CROSS_GCC="$(store_path "gcc-cross-${HOST}")"
        CROSS_GCC_LIB_STORE="$(store_path "gcc-cross-${HOST}" lib)"
        CROSS_GCC_LIBS=( "${CROSS_GCC_LIB_STORE}/lib/gcc/${HOST}"/* ) # This expands to an array of directories...
        CROSS_GCC_LIB="${CROSS_GCC_LIBS[0]}" # ...we just want the first one (there should only be one)

        export CROSS_C_INCLUDE_PATH="${CROSS_GCC_LIB}/include:${CROSS_GCC_LIB}/include-fixed:${CROSS_GLIBC}/include:${CROSS_KERNEL}/include"
        export CROSS_CPLUS_INCLUDE_PATH="${CROSS_GCC}/include/c++:${CROSS_GCC}/include/c++/${HOST}:${CROSS_GCC}/include/c++/backward:${CROSS_C_INCLUDE_PATH}"
        export CROSS_LIBRARY_PATH="${CROSS_GCC_LIB_STORE}/lib:${CROSS_GCC_LIB}:${CROSS_GLIBC}/lib:${CROSS_GLIBC_STATIC}/lib"
        ;;
    *)
        exit 1 ;;
esac

# Sanity check CROSS_*_PATH directories
IFS=':' read -ra PATHS <<< "${CROSS_C_INCLUDE_PATH}:${CROSS_CPLUS_INCLUDE_PATH}:${CROSS_LIBRARY_PATH}"
for p in "${PATHS[@]}"; do
    if [ -n "$p" ] && [ ! -d "$p" ]; then
        echo "'$p' doesn't exist or isn't a directory... Aborting..."
        exit 1
    fi
done

# Disable Guix ld auto-rpath behavior
export GUIX_LD_WRAPPER_DISABLE_RPATH=yes

# Make /usr/bin if it doesn't exist
[ -e /usr/bin ] || mkdir -p /usr/bin

# Symlink env to a conventional path
[ -e /usr/bin/env ]  || ln -s --no-dereference "$(command -v env)"  /usr/bin/env

# Determine the correct value for -Wl,--dynamic-linker for the current $HOST
case "$HOST" in
    *linux*)
        case "$HOST" in
            x86_64-linux-gnu)    glibc_dynamic_linker=/lib64/ld-linux-x86-64.so.2 ;;
            arm-linux-gnueabihf) glibc_dynamic_linker=/lib/ld-linux-armhf.so.3 ;;
            aarch64-linux-gnu)   glibc_dynamic_linker=/lib/ld-linux-aarch64.so.1 ;;
            *)                   exit 1 ;;
        esac
        ;;
esac

###########################
# Source Tarball Building #
###########################

GIT_ARCHIVE="${DIST_ARCHIVE_BASE}/${DISTNAME}.tar.gz"

# Create the source tarball if not already there
if [ ! -e "$GIT_ARCHIVE" ]; then
    mkdir -p "$(dirname "$GIT_ARCHIVE")"
    git archive --prefix="${DISTNAME}/" --output="$GIT_ARCHIVE" HEAD
fi

mkdir -p "$OUTDIR"

###########################
# Binary Tarball Building #
###########################

# CFLAGS
HOST_CFLAGS="-O2 -g"
HOST_CFLAGS+=$(find /gnu/store -maxdepth 1 -mindepth 1 -type d -exec echo -n " -ffile-prefix-map={}=/usr" \;)
HOST_CFLAGS+=" -fdebug-prefix-map=${DISTSRC}/src=."
case "$HOST" in
    *mingw*)  HOST_CFLAGS+=" -fno-ident" ;;
    *darwin*) unset HOST_CFLAGS ;;
esac

# CXXFLAGS
HOST_CXXFLAGS="$HOST_CFLAGS"

case "$HOST" in
    arm-linux-gnueabihf) HOST_CXXFLAGS="${HOST_CXXFLAGS} -Wno-psabi" ;;
esac

# LDFLAGS
case "$HOST" in
    *linux*)  HOST_LDFLAGS="-Wl,--as-needed -Wl,--dynamic-linker=$glibc_dynamic_linker -Wl,-O2" ;;
    *mingw*)  HOST_LDFLAGS="-Wl,--no-insert-timestamp" ;;
esac


# Cross-compiler flags for cmake
case "$HOST" in
    *mingw*)
        CMAKE_CROSS_FLAGS="-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=${HOST}-gcc -DCMAKE_CXX_COMPILER=${HOST}-g++"
        ;;
    *darwin*)
        case "$HOST" in
            aarch64-*) darwin_target=aarch64-apple-darwin ; darwin_arch=arm64  ;;
            x86_64-*)  darwin_target=x86_64-apple-darwin  ; darwin_arch=x86_64 ;;
        esac
        DARWIN_MIN_VERSION="${DARWIN_MIN_VERSION:-14.0}"
        DARWIN_SDK_VERSION="${DARWIN_SDK_VERSION:-26.0}"
        DARWIN_LINKER_VERSION="${DARWIN_LINKER_VERSION:-711}"
        # SDK_PATH may point to a parent directory containing the actual SDK dir.
        # Find the first child directory that contains usr/lib (the real sysroot).
        ACTUAL_SDK_PATH="${SDK_PATH}"
        if [ ! -d "${SDK_PATH}/usr/lib" ]; then
            ACTUAL_SDK_PATH=$(find "${SDK_PATH}" -maxdepth 2 -name "usr" -type d \
                              | sed 's|/usr$||' | head -1)
            if [ -z "${ACTUAL_SDK_PATH}" ] || [ ! -d "${ACTUAL_SDK_PATH}/usr/lib" ]; then
                echo "ERR: Could not find macOS SDK sysroot under ${SDK_PATH}"
                exit 1
            fi
        fi
        # Write a cmake toolchain file — flags with spaces can't be passed
        # safely on the command line via ${CMAKE_CROSS_FLAGS}.
        DARWIN_TOOLCHAIN="${DISTSRC}/darwin-toolchain.cmake"
        mkdir -p "${DISTSRC}"
        cat > "${DARWIN_TOOLCHAIN}" << EOF
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_VERSION 23.0)
set(CMAKE_AR "llvm-ar")
set(CMAKE_RANLIB "llvm-ranlib")
set(CMAKE_NM "llvm-nm")
set(CMAKE_OBJDUMP "llvm-objdump")
set(CMAKE_STRIP "llvm-strip")
set(CMAKE_C_COMPILER   "clang;--target=${darwin_target};-isysroot;${ACTUAL_SDK_PATH};-nostdlibinc;-iwithsysroot/usr/include;-iframeworkwithsysroot/System/Library/Frameworks")
set(CMAKE_CXX_COMPILER "clang++;--target=${darwin_target};-isysroot;${ACTUAL_SDK_PATH};-nostdlibinc;-iwithsysroot/usr/include/c++/v1;-iwithsysroot/usr/include;-iframeworkwithsysroot/System/Library/Frameworks")
set(CMAKE_C_FLAGS_INIT   "-arch ${darwin_arch} -mmacos-version-min=${DARWIN_MIN_VERSION} -mlinker-version=${DARWIN_LINKER_VERSION}")
set(CMAKE_CXX_FLAGS_INIT "-arch ${darwin_arch} -mmacos-version-min=${DARWIN_MIN_VERSION} -mlinker-version=${DARWIN_LINKER_VERSION} -Xclang -fno-cxx-modules")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -Wl,-platform_version,macos,${DARWIN_MIN_VERSION},${DARWIN_SDK_VERSION} -Wl,-no_adhoc_codesign")
EOF
        CMAKE_CROSS_FLAGS="--toolchain ${DARWIN_TOOLCHAIN}"
        ;;
    *linux*)
        CMAKE_CROSS_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_C_COMPILER=${HOST}-gcc -DCMAKE_CXX_COMPILER=${HOST}-g++"
        ;;
esac

mkdir -p "$DISTSRC"
(
    cd "$DISTSRC"

    # Extract the source tarball
    tar --strip-components=1 -xf "${GIT_ARCHIVE}"

    # Configure
    # shellcheck disable=SC2086
    cmake -S . -B build \
          -DCMAKE_BUILD_TYPE=Release \
          ${HOST_CFLAGS:+-DCMAKE_C_FLAGS="${HOST_CFLAGS}"} \
          ${HOST_CXXFLAGS:+-DCMAKE_CXX_FLAGS="${HOST_CXXFLAGS}"} \
          ${HOST_LDFLAGS:+-DCMAKE_EXE_LINKER_FLAGS="${HOST_LDFLAGS}"} \
          -DFETCHCONTENT_SOURCE_DIR_FTXUI="${SOURCES_PATH}/ftxui" \
          -DFETCHCONTENT_SOURCE_DIR_CATCH2="${SOURCES_PATH}/catch2" \
          ${CMAKE_CROSS_FLAGS} \

    cmake --build build -j "$JOBS" ${V:+--verbose}

    mkdir -p "$OUTDIR"

    # Install to a staging directory; this also serves as input for binary tarballs.
    INSTALLPATH="${PWD}/installed/${DISTNAME}"
    mkdir -p "${INSTALLPATH}"
    case "$HOST" in
        *darwin*)
            cmake --install build --strip --prefix "${INSTALLPATH}" --component runtime ${V:+--verbose}
            ;;
        *)
            cmake --install build --prefix "${INSTALLPATH}" --component runtime ${V:+--verbose}
            ;;
    esac

    (
        cd installed

        # Deterministically produce {non-,}debug binary tarballs ready
        # for release
        case "$HOST" in
            *mingw*)
                find "${DISTNAME}" -print0 \
                    | xargs -0r touch --no-dereference --date="@${SOURCE_DATE_EPOCH}"
                find "${DISTNAME}" \
                    | sort \
                    | zip -X@ "${OUTDIR}/${DISTNAME}-win64.zip" \
                    || ( rm -f "${OUTDIR}/${DISTNAME}-win64.zip" && exit 1 )
                ;;
            *linux*)
                find "${DISTNAME}" -print0 \
                    | sort --zero-terminated \
                    | tar --create --no-recursion --mode='u+rw,go+r-w,a+X' --null --files-from=- \
                    | gzip -9n > "${OUTDIR}/${DISTNAME}-${HOST}.tar.gz" \
                    || ( rm -f "${OUTDIR}/${DISTNAME}-${HOST}.tar.gz" && exit 1 )
                ;;
            *darwin*)
                find "${DISTNAME}" -print0 \
                    | sort --zero-terminated \
                    | tar --create --no-recursion --mode='u+rw,go+r-w,a+X' --null --files-from=- \
                    | gzip -9n > "${OUTDIR}/${DISTNAME}-${HOST}-unsigned.tar.gz" \
                    || ( rm -f "${OUTDIR}/${DISTNAME}-${HOST}-unsigned.tar.gz" && exit 1 )
                ;;
        esac
    )  # $DISTSRC/installed

)  # $DISTSRC

rm -rf "$ACTUAL_OUTDIR"
mv --no-target-directory "$OUTDIR" "$ACTUAL_OUTDIR" \
    || ( rm -rf "$ACTUAL_OUTDIR" && exit 1 )

(
    tmp="$(mktemp)"
    cd /outdir-base
    {
        echo "$GIT_ARCHIVE"
        find "$ACTUAL_OUTDIR" -type f
    } | xargs realpath --relative-base="$PWD" \
        | xargs sha256sum \
        | sort -k2 \
        > "$tmp";
    mv "$tmp" "$ACTUAL_OUTDIR"/SHA256SUMS.part
)

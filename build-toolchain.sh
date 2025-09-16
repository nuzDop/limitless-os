#!/bin/bash
# LimitlessOS Cross-Compiler Toolchain Build Script
#
# This script builds a robust x86_64-elf cross-compiler toolchain
# required for kernel and bootloader development. It is designed to be
# idempotent and safe to re-run.

# Strict Error Handling
set -euo pipefail

# --- Configuration ---
readonly PREFIX="$HOME/opt/cross"
readonly TARGET="x86_64-elf"
readonly BINUTILS_VERSION="2.41"
readonly GCC_VERSION="13.2.0"
readonly SRC_DIR="$HOME/src"
readonly BUILD_DIR="$SRC_DIR/limitless-toolchain-build"
readonly NPROC=$(nproc)

# --- Pre-flight Checks ---
check_dependencies() {
    local missing=0
    # List of executables to check with 'command -v'
    local executables=("wget" "tar" "make" "gcc" "g++" "nasm" "xorriso")
    # List of library packages to check with 'dpkg-query'
    local libraries=("libisl-dev")

    echo "Checking for required build dependencies..."

    for dep in "${executables[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            echo "  ERROR: Missing executable dependency: $dep"
            missing=1
        fi
    done

    for dep in "${libraries[@]}"; do
        if ! dpkg-query -W -f='${Status}' "$dep" 2>/dev/null | grep -q "ok installed"; then
            echo "  ERROR: Missing library dependency: $dep"
            missing=1
        fi
    done

    if [ $missing -eq 1 ]; then
        echo "Please install missing dependencies. On Debian/Ubuntu:"
        echo "sudo apt-get install build-essential libisl-dev xorriso nasm"
        exit 1
    fi
}

# --- Main Build Logic ---
main() {
    check_dependencies

    export PATH="$PREFIX/bin:$PATH"

    # Create directories
    mkdir -p "$SRC_DIR" "$BUILD_DIR"

    # --- Download and Extract ---
    cd "$SRC_DIR"
    local binutils_src="binutils-$BINUTILS_VERSION"
    local gcc_src="gcc-$GCC_VERSION"

    if [ ! -f "$binutils_src.tar.gz" ]; then
        echo "Downloading binutils..."
        wget "https://ftp.gnu.org/gnu/binutils/$binutils_src.tar.gz"
    fi
    if [ ! -f "$gcc_src.tar.gz" ]; then
        echo "Downloading GCC..."
        wget "https://ftp.gnu.org/gnu/gcc/$gcc_src/$gcc_src.tar.gz"
    fi

    if [ ! -d "$binutils_src" ]; then
        echo "Extracting binutils..."
        tar -xzf "$binutils_src.tar.gz"
    fi
    if [ ! -d "$gcc_src" ]; then
        echo "Extracting GCC..."
        tar -xzf "$gcc_src.tar.gz"
    fi

    # --- Build Binutils ---
    echo "Building binutils..."
    rm -rf "$BUILD_DIR/build-binutils"
    mkdir -p "$BUILD_DIR/build-binutils"
    cd "$BUILD_DIR/build-binutils"
    "$SRC_DIR/$binutils_src/configure" --target="$TARGET" --prefix="$PREFIX" \
        --with-sysroot --disable-nls --disable-werror
    make -j"$NPROC"
    make install

    # --- Build GCC ---
    echo "Building GCC..."
    rm -rf "$BUILD_DIR/build-gcc"
    mkdir -p "$BUILD_DIR/build-gcc"
    cd "$BUILD_DIR/build-gcc"
    "$SRC_DIR/$gcc_src/configure" --target="$TARGET" --prefix="$PREFIX" \
        --disable-nls --enable-languages=c,c++ --without-headers
    make all-gcc -j"$NPROC"
    make all-target-libgcc -j"$NPROC"
    make install-gcc
    make install-target-libgcc

    # --- Finalization ---
    echo -e "\n✅ Toolchain built successfully!"
    echo "Add the following line to your ~/.bashrc or ~/.profile:"
    echo "export PATH=\"$PREFIX/bin:\$PATH\""
}

main "$@"

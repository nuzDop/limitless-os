#!/bin/bash
# Build x86_64-elf cross-compiler toolchain

# Exit immediately if a command exits with a non-zero status.
set -e

PREFIX="$HOME/opt/cross"
TARGET=x86_64-elf
PATH="$PREFIX/bin:$PATH"

BINUTILS_VERSION=2.41
GCC_VERSION=13.2.0

# Create source directory if it doesn't exist
mkdir -p ~/src
cd ~/src

# Download sources (if they don't exist)
[ ! -f binutils-$BINUTILS_VERSION.tar.gz ] && wget https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.gz
[ ! -f gcc-$GCC_VERSION.tar.gz ] && wget https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION.tar.gz

# Extract (if directories don't exist)
[ ! -d binutils-$BINUTILS_VERSION ] && tar -xzf binutils-$BINUTILS_VERSION.tar.gz
[ ! -d gcc-$GCC_VERSION ] && tar -xzf gcc-$GCC_VERSION.tar.gz

# --- Clean up previous builds ---
rm -rf build-binutils
rm -rf build-gcc

# Build binutils
mkdir build-binutils
cd build-binutils
../binutils-$BINUTILS_VERSION/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror
make -j$(nproc)
make install
cd ..

# Build GCC
# The ISL library is recommended for GCC optimization
sudo apt-get install -y libisl-dev

mkdir build-gcc
cd build-gcc
../gcc-$GCC_VERSION/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers
make all-gcc -j$(nproc)
make all-target-libgcc -j$(nproc)
make install-gcc
make install-target-libgcc
cd ..

echo "✅ Toolchain built successfully!"
echo "Add to PATH: export PATH=$PREFIX/bin:\$PATH"

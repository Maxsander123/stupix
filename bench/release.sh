#!/bin/sh
# =====================================================
# Maintainer tool: rebuild the bundled static binaries in bin/
# inside a Debian container (cross compilers + qemu-user to run
# the selftest on every architecture). Needs docker.
# =====================================================
set -e
cd "$(dirname "$0")"
docker run --rm -v "$PWD":/src -w /src debian:trixie sh -ec '
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        gcc libc6-dev gcc-aarch64-linux-gnu libc6-dev-arm64-cross \
        gcc-riscv64-linux-gnu libc6-dev-riscv64-cross gcc-mingw-w64-x86-64 qemu-user >/dev/null
    mkdir -p bin
    ./build.sh --static --out bin/stupix-bench-linux-x86_64
    CC=aarch64-linux-gnu-gcc ./build.sh --static --out bin/stupix-bench-linux-aarch64
    CC=riscv64-linux-gnu-gcc ./build.sh --static --out bin/stupix-bench-linux-riscv64
    CC=x86_64-w64-mingw32-gcc ./build.sh --static --out bin/stupix-bench-windows-x86_64.exe
    strip bin/stupix-bench-linux-x86_64
    aarch64-linux-gnu-strip bin/stupix-bench-linux-aarch64
    riscv64-linux-gnu-strip bin/stupix-bench-linux-riscv64
    x86_64-w64-mingw32-strip bin/stupix-bench-windows-x86_64.exe
    echo "== selftest x86_64";  bin/stupix-bench-linux-x86_64 --selftest
    echo "== selftest aarch64"; qemu-aarch64 bin/stupix-bench-linux-aarch64 --selftest
    echo "== selftest riscv64"; qemu-riscv64 bin/stupix-bench-linux-riscv64 --selftest
    chown -R '"$(id -u):$(id -g)"' bin build
'
ls -l bin

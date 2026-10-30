#!/bin/sh
# =====================================================
# Run stupix-bench on any machine, with no prior setup:
#   1. build from source if a C compiler exists (full feature set, GPU via OpenCL/NVML)
#   2. otherwise use the bundled static binary for this OS/arch (bin/)
#   3. otherwise install a compiler with the system package manager and build
# All arguments are passed to stupix-bench (see ./run.sh --help).
# Set STUPIX_BENCH_NO_INSTALL=1 to forbid step 3.
# =====================================================
DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/build/stupix-bench"

msg() { echo "[stupix-bench] $*" >&2; }

have_cc() {
    for c in cc gcc clang; do
        command -v "$c" >/dev/null 2>&1 && return 0
    done
    return 1
}

install_cc() {
    [ "${STUPIX_BENCH_NO_INSTALL:-0}" = 1 ] && return 1
    SUDO=""
    if [ "$(id -u)" != 0 ]; then
        if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then SUDO="sudo"; else return 1; fi
    fi
    msg "no C compiler found, installing one"
    if command -v apt-get >/dev/null 2>&1; then
        $SUDO apt-get update -qq && DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -qq gcc libc6-dev
    elif command -v dnf >/dev/null 2>&1; then
        $SUDO dnf install -y -q gcc glibc-devel
    elif command -v yum >/dev/null 2>&1; then
        $SUDO yum install -y -q gcc glibc-devel
    elif command -v zypper >/dev/null 2>&1; then
        $SUDO zypper -n -q install gcc glibc-devel
    elif command -v pacman >/dev/null 2>&1; then
        $SUDO pacman -Sy --noconfirm --needed gcc
    elif command -v apk >/dev/null 2>&1; then
        $SUDO apk add -q build-base linux-headers
    elif command -v pkg >/dev/null 2>&1; then
        $SUDO pkg install -y gcc
    elif command -v xbps-install >/dev/null 2>&1; then
        $SUDO xbps-install -Sy gcc
    elif command -v emerge >/dev/null 2>&1; then
        $SUDO emerge --quiet sys-devel/gcc
    else
        return 1
    fi
}

arch() {
    case "$(uname -m)" in
        x86_64|amd64) echo x86_64 ;;
        aarch64|arm64) echo aarch64 ;;
        riscv64) echo riscv64 ;;
        *) uname -m ;;
    esac
}

# rebuild when missing or older than the sources
stale() {
    [ ! -x "$BIN" ] && return 0
    [ -n "$(find "$DIR/src" -newer "$BIN" -type f 2>/dev/null | head -1)" ]
}

if stale; then
    if have_cc || install_cc; then
        "$DIR/build.sh" >&2 || rm -f "$BIN"
    fi
fi

if [ ! -x "$BIN" ]; then
    OS=$(uname -s | tr 'A-Z' 'a-z')
    PRE="$DIR/bin/stupix-bench-$OS-$(arch)"
    if [ -f "$PRE" ]; then
        chmod +x "$PRE" 2>/dev/null
        msg "using bundled static binary $(basename "$PRE") (no GPU compute in static builds)"
        BIN="$PRE"
    else
        msg "no compiler available and no bundled binary for $OS-$(arch)"
        msg "install any C compiler (gcc/clang) and re-run"
        exit 1
    fi
fi

exec "$BIN" "$@"

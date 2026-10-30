#!/bin/sh
# =====================================================
# Build stupix-bench with nothing but a C compiler.
#   ./build.sh                 native build -> build/stupix-bench
#   ./build.sh --static        fully static Linux binary (no GPU compute)
#   ./build.sh --no-asm        portable C kernels only
#   CC=aarch64-linux-gnu-gcc ./build.sh --static --out build/stupix-bench-linux-aarch64
# Falls back to the portable C kernels if the assembler step fails.
# =====================================================
set -e
cd "$(dirname "$0")"

STATIC=0
NOASM=0
OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --static) STATIC=1 ;;
        --no-asm) NOASM=1 ;;
        --out) shift; OUT="$1" ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

if [ -z "$CC" ]; then
    for c in cc gcc clang; do
        if command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
    done
fi
if [ -z "$CC" ]; then
    echo "no C compiler found (cc, gcc or clang)" >&2
    exit 1
fi

TARGET_OS=$($CC -dumpmachine 2>/dev/null || uname -s)
CFLAGS="${CFLAGS:--O2} -std=gnu99 -ffp-contract=off -fno-strict-aliasing -Wall -Wno-unused-result -Wno-format-truncation"
LIBS="-lm"
EXE=""
case "$TARGET_OS" in
    *mingw*|*windows*|*cygwin*|MINGW*|MSYS*) EXE=".exe" ;;
    *darwin*|*apple*|Darwin) ;;
    *linux*|Linux) LIBS="$LIBS -lpthread -ldl" ;;
    *) LIBS="$LIBS -lpthread" ;;
esac
if [ "$STATIC" = 1 ]; then
    CFLAGS="$CFLAGS -DSTX_NO_DLOPEN"
    LDFLAGS="$LDFLAGS -static"
    LIBS=$(echo "$LIBS" | sed 's/-ldl//')
fi

mkdir -p build
[ -n "$OUT" ] || OUT="build/stupix-bench$EXE"
SRCS="src/main.c src/platform.c src/kernels.c src/report.c src/sensors.c src/mod_cpu.c src/mod_mem.c
      src/mod_disk.c src/mod_gpu.c src/mod_chipset.c src/mod_bmc.c src/mod_load.c"

build() {
    # shellcheck disable=SC2086
    $CC $CFLAGS $1 -o "$OUT" $SRCS $2 $LDFLAGS $LIBS
}

if [ "$NOASM" = 0 ] && build "" "src/asm/kernels.S" 2>build/asm-build.log; then
    :
else
    [ "$NOASM" = 0 ] && echo "assembly build failed (see build/asm-build.log), using portable C kernels" >&2
    build "-DSTX_NO_ASM" ""
fi
echo "built $OUT ($($CC --version 2>/dev/null | head -1))"

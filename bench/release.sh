#!/bin/sh
# =====================================================
# Maintainer tool: rebuild the bundled binaries in bin/ for every
# platform with zig cc (cross compiler with bundled libcs, no SDKs)
# inside a Debian container, then run each Linux build under the
# matching qemu-user CPU and the Windows builds under wine.
# Needs docker and network access.
# =====================================================
set -e
cd "$(dirname "$0")"
docker run --rm -v "$PWD":/src -w /src debian:trixie bash -ec '
    export DEBIAN_FRONTEND=noninteractive ZIG_GLOBAL_CACHE_DIR=/tmp/zig-cache ZIG_LOCAL_CACHE_DIR=/tmp/zig-cache
    dpkg --add-architecture i386
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends curl ca-certificates xz-utils python3 qemu-user \
        wine wine64 wine32:i386 gcc-arm-linux-gnueabi libc6-dev-armel-cross >/dev/null
    url=$(curl -fsSL https://ziglang.org/download/index.json | python3 -c "
import json, sys
d = json.load(sys.stdin)
v = sorted((k for k in d if k != \"master\"), key=lambda s: [int(x) for x in s.split(\".\")])[-1]
print(d[v][\"x86_64-linux\"][\"tarball\"])")
    curl -fsSL "$url" | tar xJ -C /opt
    export PATH="$(echo /opt/zig-*):$PATH"
    echo "zig $(zig version)"

    mkdir -p bin
    rm -f bin/stupix-bench-*
    failed=""
    # name  zig-target  static  extra-cflags
    while read -r name target static extra; do
        [ -z "$name" ] && continue
        ext=""; case "$target" in *windows*) ext=".exe" ;; esac
        flag=""; [ "$static" = yes ] && flag="--static"
        if CC="zig cc -target $target $extra -s" TARGET_OS="$target" \
            ./build.sh $flag --out "bin/stupix-bench-$name$ext" >/dev/null 2>build/zig-$name.log; then
            impl=$(grep -ao "assembly ([a-z0-9_]*)\|portable C + assembly clock ([a-z0-9_]*)\|portable C ([a-z0-9_]*)" \
                "bin/stupix-bench-$name$ext" | head -1)
            printf "  built %-24s %s\n" "$name" "$impl"
        else
            printf "  FAILED %-23s (build/zig-%s.log)\n" "$name" "$name"
            failed="$failed $name"
        fi
    done <<EOF
linux-x86_64       x86_64-linux-musl       yes
linux-x86          x86-linux-musl          yes -mcpu=i686
linux-aarch64      aarch64-linux-musl      yes
linux-arm          arm-linux-musleabihf    yes -mcpu=arm1176jzf_s
linux-riscv64      riscv64-linux-musl      yes
linux-riscv32      riscv32-linux-musl      yes
linux-ppc64le      powerpc64le-linux-musl  yes
linux-s390x        s390x-linux-musl        yes
linux-loongarch64  loongarch64-linux-musl  yes
macos-aarch64      aarch64-macos           no
macos-x86_64       x86_64-macos            no
windows-x86_64     x86_64-windows-gnu      yes
windows-x86        x86-windows-gnu         yes
windows-aarch64    aarch64-windows-gnu     yes
EOF

    # ARMv5 soft-float: zig'"'"'s libc lacks the atomic helpers there, use Debian'"'"'s armel gcc (static glibc)
    if CC=arm-linux-gnueabi-gcc ./build.sh --static --out bin/stupix-bench-linux-armel >/dev/null 2>build/gcc-armel.log; then
        arm-linux-gnueabi-strip bin/stupix-bench-linux-armel
        printf "  built %-24s %s\n" linux-armel "$(grep -ao "portable C + assembly clock (arm)\|portable C (arm)" bin/stupix-bench-linux-armel | head -1)"
    else
        printf "  FAILED linux-armel (build/gcc-armel.log)\n"; failed="$failed linux-armel"
    fi

    echo "== running every Linux build under qemu-user, Windows builds under wine"
    run() {
        name=$1; shift
        f="bin/stupix-bench-$name"
        [ -f "$f" ] || [ -f "$f.exe" ] || return 0
        st=$("$@" --selftest 2>&1 | tr -d "\r" | tail -1)
        ov=$("$@" --no-color --quick --time 0.05 --only cpu,mem 2>&1 | tr -d "\r" | grep -a "OVERALL" | tr -s " ")
        printf "  %-18s %-16s %s\n" "$name" "$st" "${ov:-NO OUTPUT}"
        case "$st" in "selftest passed") ;; *) failed="$failed $name(run)" ;; esac
    }
    run linux-x86_64      bin/stupix-bench-linux-x86_64
    run linux-x86         qemu-i386 -cpu pentium3 bin/stupix-bench-linux-x86
    run linux-aarch64     qemu-aarch64 bin/stupix-bench-linux-aarch64
    run linux-arm         qemu-arm -cpu arm1176 bin/stupix-bench-linux-arm
    run linux-armel       qemu-arm -cpu arm926 bin/stupix-bench-linux-armel
    run linux-riscv64     qemu-riscv64 bin/stupix-bench-linux-riscv64
    run linux-riscv32     qemu-riscv32 bin/stupix-bench-linux-riscv32
    run linux-ppc64le     qemu-ppc64le bin/stupix-bench-linux-ppc64le
    run linux-s390x       qemu-s390x bin/stupix-bench-linux-s390x
    run linux-loongarch64 qemu-loongarch64 bin/stupix-bench-linux-loongarch64
    export WINEDEBUG=-all WINEPREFIX=/tmp/wine
    run windows-x86_64    wine bin/stupix-bench-windows-x86_64.exe
    run windows-x86       wine bin/stupix-bench-windows-x86.exe
    chown -R '"$(id -u):$(id -g)"' bin build
    [ -z "$failed" ] || echo "problems:$failed"
'
ls -l bin

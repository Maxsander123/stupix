# stupix-bench

Portable whole-system benchmark and hardware health check. It rates CPU,
memory, storage, GPU, chipset/PCIe, BMC, thermals and power, and gives every
part a score where **1000 = reference system**.

- **No dependencies.** Written in C99 with hand-written assembly kernels and
  only libc underneath. It doesn't need ipmitool, smartctl, fio, lspci,
  dmidecode, nvidia-smi or any SDK. IPMI, NVMe SMART and SATA SMART talk to
  the kernel through ioctls directly. GPU libraries (OpenCL, NVML) are loaded
  at runtime from the installed driver when they exist.
- **Runs anywhere.** `run.sh` builds from source when a C compiler exists,
  otherwise uses a bundled static binary, otherwise installs gcc with the
  system package manager.
- **Safe on production disks.** Raw block devices are opened read-only. The
  only write is a temporary file in `--disk-path`, deleted afterwards.

## Quick start

    ./run.sh                    # all modules, ~75 s
    ./run.sh --quick            # ~30 s
    sudo ./run.sh --full        # long run with every hardware probe
    ./run.sh --only cpu,mem     # selected modules
    ./run.sh --json report.json # machine readable report
    ./run.sh --selftest         # verify assembly kernels against C reference

Without a shell (Windows): `bin\stupix-bench-windows-x86_64.exe --quick`.

Run as root on Linux to get the BMC, SMART, raw device, DIMM (SMBIOS) and
RAPL data.

## Modules

| Module    | Measures | Health checks |
|-----------|----------|---------------|
| `cpu`     | effective clock (asm add-chain), integer + float64 throughput 1 thread / all threads, branchy sort, ChaCha20, multi-core speedup; vendor, model, features (cpuid), caches, governor | scaling efficiency |
| `mem`     | read / write / copy bandwidth, latency ladder 16 KiB to 512 MiB (L1 to DRAM); DIMM slots, type, speed, part number, ECC from raw SMBIOS | DIMM below rated speed, EDAC corrected/uncorrectable errors |
| `disk`    | file test: seq write/read 1M, random 4K read QD1 + 16 threads, 4K write+fsync; raw devices (read-only): seq read, random 4K QD1 + 16 threads; NVMe temperature, wear, power-on hours, TB written | NVMe critical warning / media errors / wear, SATA SMART status, reallocated / pending sectors |
| `gpu`     | all DRM GPUs (any vendor), NVIDIA temperature/power/clocks/PCIe via NVML; OpenCL fp32 compute, VRAM bandwidth, host-to-device PCIe transfer | |
| `chipset` | host bridge / PCH, firmware (UEFI, Secure Boot, TPM), NUMA, PCIe topology, negotiated PCIe bandwidth, IOMMU | links running below their width, AER fatal / non-fatal / correctable errors |
| `bmc`     | in-band IPMI via `/dev/ipmi0`: vendor, firmware, IPMI version, SEL fill, SDR count, BMC IP/MAC, DCMI system power, round-trip latency (loads `ipmi_si`/`ipmi_devintf` itself when SMBIOS announces a BMC) | BMC self test, chassis power/cooling/drive faults, SEL nearly full |
| `load`    | sustained all-core load: throughput start vs. end, clock, CPU temperature, all hwmon temperatures and fans, RAPL package power, battery and BMC wall power, performance per watt | throttling, thermal limit reached |

## Scores

Each scored metric is `1000 × value / reference` (or `reference / value` where
lower is better). A section score is the geometric mean of its metrics, and the
overall score is the geometric mean of the section scores. Sections without
scored metrics, such as a missing BMC, are left out instead of counting as zero.

Reference system: Dell Precision 3581, Intel Core i7-13700H (6P+8E, 20 threads)
on AC power, dual channel DDR5, Samsung PM9A1 NVMe (ext4), NVIDIA RTX A500
Laptop GPU. The GPU bandwidth and PCIe transfer references use the card's
nominal values. The BMC reference is 10 ms per KCS round trip, a typical
server value. The reference values sit at the top of each `src/mod_*.c`.

Comparable results need the same conditions: AC power, idle system, same
`--time`. The report notes when it runs on battery, under a VM, or without root.

## Platforms

| Platform | Status |
|----------|--------|
| Linux x86_64 | native build, all modules tested |
| Linux aarch64, riscv64 | static binaries, asm selftest and modules run under qemu-user |
| Windows x86_64 | binary cross-built with MinGW (asm kernels included), not run on Windows yet; CPU/memory/disk/GPU work, hardware probes are Linux-only |
| macOS, FreeBSD/OpenBSD/NetBSD, other Unix | code paths present (sysctl, F_NOCACHE, pthreads), build from source with `./build.sh`; not tested yet |
| other CPUs (ppc64, s390x, mips, loongarch, 32-bit) | portable C kernels (`STX_NO_ASM` automatically) |

## Building

    ./build.sh                  # native, build/stupix-bench
    ./build.sh --static         # static Linux binary (GPU compute disabled: no dlopen)
    ./build.sh --no-asm         # portable C kernels only
    CC=x86_64-w64-mingw32-gcc ./build.sh --static --out stupix-bench.exe
    ./release.sh                # rebuild bin/ for all targets in docker + qemu selftests

Only a C compiler is needed, not make. If the assembler step fails on an
unusual toolchain, `build.sh` falls back to the C kernels automatically.

## Layout

    src/asm/kernels.S   hand-written kernels: x86_64 (SysV + Win64), aarch64, riscv64
    src/kernels.c       bit-exact C twins, dispatch, selftest, calibration
    src/platform.c      timers, threads, files, dlopen for POSIX and Windows
    src/report.c        console report, scoring, JSON
    src/sensors.c       temperatures, clocks, RAPL, battery (sysfs)
    src/mod_*.c         one file per module
    bin/                bundled static binaries (rebuilt by release.sh)

The asm kernels use only caller-saved registers (Win64 xmm6-9 are saved
explicitly), so they need no stack frames or unwind tables.
`--selftest` checks every kernel against its C twin. The integer, float, read
and pointer-chase results must match bit for bit.

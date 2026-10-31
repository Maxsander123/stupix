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

Windows: `run.cmd --quick` (picks the x86, x64 or ARM64 build).

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

`bin/` ships ready-to-run binaries. The Linux ones are fully static and run
on any distribution and libc. `run.sh` and `run.cmd` pick the right one
automatically when no compiler is installed.

| Binary | CPUs | Kernels | Tested |
|--------|------|---------|--------|
| `linux-x86_64` | any x86-64 | full asm | native |
| `linux-x86` | i686 and newer (32-bit) | C + asm clock/cpuid | native 32-bit (setarch) + qemu pentium3 |
| `linux-aarch64` | any ARMv8+ (Raspberry Pi 3/4/5, Graviton, Ampere, Jetson, ...) | full asm | qemu |
| `linux-arm` | ARMv6+ with VFP, hard-float (every Raspberry Pi incl. Zero/1, ARMv7 boards) | C + asm clock | qemu arm1176 |
| `linux-armel` | ARMv5TE+ soft-float (old NAS/routers/boards) | C + asm clock | qemu arm926 |
| `linux-riscv64` | RV64GC (VisionFive, Milk-V, SiFive, ...) | full asm | qemu |
| `linux-riscv32` | RV32 with Linux | C | qemu |
| `linux-ppc64le`, `linux-s390x`, `linux-loongarch64` | POWER8+, IBM Z, LoongArch | C | qemu |
| `windows-x86_64` | x64 | full asm | wine |
| `windows-x86` | 32-bit x86 | C + asm clock/cpuid | wine (32-bit) |
| `windows-aarch64` | Windows on ARM | full asm | build only |
| `macos-aarch64` | Apple Silicon (ad-hoc signed) | full asm | build only |
| `macos-x86_64` | Intel Macs | full asm | build only |

Every other OS or CPU with a C compiler (FreeBSD, OpenBSD, NetBSD, other
Unix, 32-bit MIPS, ...) builds from source with `./build.sh`. It uses the
portable C kernels when no assembly exists for that CPU.

CPU identification covers x86 (cpuid), ARM (implementer/part decoding, e.g.
Cortex-A76, Neoverse-N1, Kunpeng, big.LITTLE combinations) and RISC-V
(`uarch`/`isa`). Hardware probes (SMART, IPMI, PCIe, sensors, RAPL) are
Linux-only. On macOS and Windows, CPU, memory, disk and GPU run.

## Building

    ./build.sh                  # native, build/stupix-bench
    ./build.sh --static         # static Linux binary (GPU compute disabled: no dlopen)
    ./build.sh --no-asm         # portable C kernels only
    CC=x86_64-w64-mingw32-gcc ./build.sh --static --out stupix-bench.exe
    ./release.sh                # rebuild bin/ for all 15 targets (zig cc in docker),
                                # then run them under qemu-user / wine

Only a C compiler is needed, not make. If the assembler step fails on an
unusual toolchain, `build.sh` falls back to the C kernels automatically.

## Layout

    src/asm/kernels.S   hand-written kernels: x86_64 (SysV + Win64), aarch64, riscv64,
                        clock/cpuid for 32-bit x86 and ARM
    src/kernels.c       bit-exact C twins, dispatch, selftest, calibration
    src/platform.c      timers, threads, files, dlopen for POSIX and Windows
    src/report.c        console report, scoring, JSON
    src/sensors.c       temperatures, clocks, RAPL, battery (sysfs)
    src/mod_*.c         one file per module
    bin/                bundled binaries for 15 platforms (rebuilt by release.sh)
    run.sh / run.cmd    launchers for Unix / Windows

The asm kernels use only caller-saved registers (Win64 xmm6-9 are saved
explicitly), so they need no stack frames or unwind tables.
`--selftest` checks every kernel against its C twin. The integer, float, read
and pointer-chase results must match bit for bit.

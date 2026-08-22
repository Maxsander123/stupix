# stupix

Script repository for the Stupix live OS.

This repository is automatically cloned to /opt/stupix after each boot.
The file auto.sh is then executed automatically by the stupix-init service.

## Repository contents

    auto.sh   - Main diagnostic script, executed on every boot

## What auto.sh does

auto.sh collects diagnostic information about the server and writes each
section into a separate log file under /var/log/stupix/.

Sections and their log files:

    network.log        - IP addresses, routing table, DNS
    system-info.log    - Hostname, kernel, BIOS, system manufacturer and model
    cpu.log            - CPU information and topology
    memory.log         - RAM overview and module details (dmidecode)
    storage.log        - Block devices, NVMe, SCSI, RAID, partitions
    smart.log          - S.M.A.R.T. data for all detected disks
    pci.log            - PCI devices
    usb.log            - USB devices
    ipmi.log           - IPMI chassis, BMC info, FRU, SDR sensors, SEL events
    hardware-full.log  - Full lshw hardware inventory
    auto.log           - Main log with timestamps for all sections

## Adding custom scripts

Place additional scripts in this repository.
They can be called from auto.sh or executed manually after boot.

## Build system

See https://github.com/Maxsander123/stupix-build for the live-build configuration.

## Boot flow

    USB/ISO Boot
      -> GRUB
      -> Kernel + Initrd
      -> live-boot (squashfs + OverlayFS in RAM)
      -> systemd
      -> DHCP on all ethernet interfaces
      -> stupix-init.service
           -> git clone https://github.com/Maxsander123/stupix /opt/stupix
           -> bash /opt/stupix/auto.sh
      -> Stupix ready

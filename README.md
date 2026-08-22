# stupix

Scripts for the Stupix server diagnostics live boot system.

This repository is automatically cloned to /opt/stupix after boot.

## auto.sh

Executed automatically after boot by stupix-init.service.

Runs the following diagnostic sections and writes each to a separate log file
under /var/log/stupix/:

    network.log           IP addresses, routing, DNS
    system-info.log       hostname, kernel, manufacturer, model, BIOS
    cpu.log               CPU model, cores, threads
    memory.log            RAM overview, DIMM slots
    storage.log           block devices, NVMe, SCSI, RAID
    smart.log             S.M.A.R.T. status for all disks
    pci.log               PCI devices
    usb.log               USB devices
    ipmi.log              chassis status, BMC info, FRU, sensors, SEL
    hardware-full.log     full lshw output
    auto.log              main log with timestamps

## Adding custom scripts

Add any .sh files to this repository. They will be available under
/opt/stupix/ after boot.

## Build system

https://github.com/Maxsander123/stupix-build

## Useful commands

    ipmitool chassis status
    ipmitool sdr list
    ipmitool sel list
    ipmitool fru print
    ipmitool -I lanplus -H <BMC-IP> -U root -P <pass> chassis status
    ipmi-sensors
    bmc-info
    dmidecode -t system
    smartctl -a /dev/sdX
    lshw -short
    nvme list
    nmap -sn 192.168.x.0/24
    tcpdump -i eth0
    stress-ng --cpu 4 --timeout 60
    memtester 1G 1

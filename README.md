# Stupix Scripts

Dieses Repository enthält die Scripts für das **Stupix Live-Boot-System**.

Nach dem Booten wird dieses Repo automatisch nach `/opt/stupix` geklont und `auto.sh` ausgeführt.

## auto.sh

Wird direkt nach dem Boot ausgeführt und:
- Zeigt System-Informationen (CPU, RAM, Disks)
- Prüft IPMI/iDRAC-Status
- Führt S.M.A.R.T.-Checks durch
- Zeigt FRU-Inventory und Sensor-Daten
- Gibt eine Übersicht nützlicher Diagnose-Befehle

## Eigene Scripts hinzufügen

Einfach neue `.sh` Dateien in dieses Repo committen.  
Sie sind nach dem Boot unter `/opt/stupix/` verfügbar.

## Build-System

Das ISO-Build-System liegt in: https://github.com/Maxsander123/stupix-build

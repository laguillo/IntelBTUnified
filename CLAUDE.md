# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A single kext (Lilu plugin + IOKit driver) replacing the upstream `IntelBluetoothFirmware`, `IntelBTPatcher` and `BlueToolFixup` kexts for Intel Bluetooth controllers on Hackintosh (macOS 12+, OpenCore). Tested only on an Intel AX210 under macOS Tahoe. Upstream sources: OpenIntelWireless/IntelBluetoothFirmware, acidanthera/BrcmPatchRAM.

## Build (Command Line Tools only, no Xcode)

Run from the repository root:

```
make deps                          # clone MacKernelSDK + Lilu into deps/ at the commits pinned in the Makefile
make clean; make FW="ibt-0041-*"   # AX210-only firmware (~630 KB kext)
make clean; make                   # all firmwares (~11 MB)
make DEBUG=1                       # enables DBGLOG
```

- Always `make clean` when changing `FW`: `build/FwBinary.cpp` does not depend on that variable.
- Output is `build/IntelBTUnified.kext`; `make dist` zips the AX210-only and full variants into `dist/`. `build/`, `deps/` and `dist/` are git-ignored. The kext is unsigned.
- There are no tests or linters. Verification so far is static: compile, then diff `nm -u` against the kernel (`nm -gj /System/Library/Kernels/kernel`) and Lilu's exports to confirm every undefined symbol resolves.
- The build does not use Xcode, so `Info.plist` and the `kmod_info` stub are generated (`scripts/gen_plist.py`, `scripts/kmod_info.c.in`). The kernel entry points `_start`/`_stop` come from MacKernelSDK's `libkmodc++`/`libkmod`; do not define your own (the upstream `zutil.c` did, which breaks static constructors/OSMetaClass registration).

## Architecture

One bundle, three cooperating pieces:

1. **Lilu plugin** — `src/IntelBTUnified.cpp`, single `PluginConfiguration` (Monterey..Tahoe). Merges two upstream plugins:
   - an HCI hook on `IOUSBHostDevice::deviceRequest` (after `IOUSBHostFamily` loads) that resends a random BLE address before `LE_SET_SCAN_PARAM` on Intel controllers listed in `devices.txt`;
   - `cs_validate_page` patching of `/usr/sbin/BlueTool` and `/usr/sbin/bluetoothd` in memory (byte patterns tied to macOS builds). Each patch logs once when first applied.
2. **IOKit firmware driver** — `src/driver/` (`IntelBluetoothFirmware` class, `BtIntel*`, `IntelBluetoothOpsGen1/2/3`, `USBDeviceController`). Matches the USB device, picks the generation from `devices.txt`, downloads the `.sfi`/`.ddc` firmware over bulk pipes. Only Gen3 (AX210/AX211/…) has been exercised on this machine.
3. **Generated pieces** (all under `build/`, driven by `devices.txt`, the single source of truth for supported USB product IDs): `Devices.h` (driver and plugin tables), `Info.plist` personalities, and `FwBinary.cpp` (zlib-compressed firmware from `fw/`, with the uncompressed size stored).

Boot-args: `-ibtoff` / `-ibtdbg` / `-ibtbeta` plus the legacy `-ibtcompat*` and `-btlfx*` spellings; feature flags `-btlfxboardid`, `-btlfxallowanyaddr`, `-btlfxnvramcheck`.

## Things that are easy to get wrong

- Intel AX210 is USB `8087:0032` (AX201 is `0026`). The bundle id is `local.IntelBTUnified`; the Lilu plugin IOService class is named after `PRODUCT_NAME` (`IntelBTUnified`).
- Never load this kext together with the three old ones; patches would apply twice. In OpenCore it must come after Lilu in `Kernel → Add`.
- After a warm reboot the chip keeps its firmware, so the driver downloads nothing and `fw_name` may be `(already running)`; that is normal.
- Kernel code cannot be exercised by building: it only runs after the kext is installed into the EFI and the machine reboots. Do not modify a mounted EFI (`/Volumes/EFI`) without being asked.
- Kernel logs are not in the unified log. `sudo dmesg` is the only source, its 128 KB buffer is flooded by ItlIwx Wi-Fi scan lines, and `sudo` cannot prompt inside Claude Code (the user must run it in their own Terminal and dump to a file). Boot-arg `msgbuf=1048576` enlarges the buffer.
- License is GPL-3.0-only (inherited); the firmware license is `licenses/iwlwifi-firmware.txt`.

See `README.md` for install steps and the list of fixes made over the upstream code.

# IntelBTUnified

**A single kext for Intel Bluetooth on Hackintosh.** It replaces `IntelBluetoothFirmware` + `IntelBTPatcher` + `BlueToolFixup`
(a Lilu plugin plus the firmware-loading driver in the same bundle). macOS 12 (Monterey) or later only, Intel/x86_64, OpenCore.

**English** · [Español](README.es.md)

## Status

| | |
|---|---|
| Tested | **Intel AX210** (USB `8087:0032`), macOS 26.7 (Tahoe), Core i7‑6700T, OpenCore, Lilu 1.7.3. Loads the firmware, connects and plays A2DP audio (classic Bluetooth headphones). |
| Untested | The other chips in `devices.txt`, macOS 12 – 25, and BLE devices (the random-address patch only matters for BLE). |
| Unconfirmed | The kernel log lines from the patches (`ibtp`, `btlfx`): the `dmesg` buffer had already lost them when it was checked. |

Use at your own risk: this is unsigned kernel code. Keep the original kexts around so you can roll back.

## Supported chips

The list comes from [`devices.txt`](devices.txt), the single source of truth (it generates the driver table and the `Info.plist` personalities).
Product IDs with vendor `0x8087`: `0025` `0026` `0029` `0032` `0033` `0035` `0036` `0038` `07dc` `0a2a` `0a2b` `0aa7` `0aaa`.
An ID that is not in the table is rejected instead of being silently treated as Gen2. To add a chip: one line in `devices.txt`
(plus its firmware in `fw/` if needed) and rebuild. In practice only the Gen3 path (AX210) has been exercised.

## Installation (OpenCore)

1. Download the zip from [Releases](../../releases) (or build it, see below): `…-AX210.zip` contains only the AX210/AX211 firmware
   (≈0.6 MB); `…-full.zip` contains every firmware (≈11 MB).
2. Copy `IntelBTUnified.kext` to `EFI/OC/Kexts/`.
3. In `config.plist` → `Kernel → Add`: **remove `IntelBluetoothFirmware`, `IntelBTPatcher` and `BlueToolFixup`** and add this one **after Lilu**.
   Loading them together would apply the patches twice.
4. Reboot and check:
   - `ioreg -rc IntelBluetoothFirmware | grep fw_name` → `ibt-0041-0041.sfi` on an AX210 (`(already running)` if the chip kept its
     firmware across a reboot without a power cycle; that is normal).
   - `system_profiler SPBluetoothDataType` → `State: On`.
   - `ioreg -c IntelBTUnified -r` → the Lilu plugin is active (`VersionInfo`).

### Boot-args

| Argument | Effect |
|---|---|
| `-ibtoff` | Disables the plugin (patches). `-ibtcompatoff` and `-btlfxoff` also work. |
| `-ibtdbg` | Debug logging (`-ibtcompatdbg`, `-btlfxdbg`). |
| `-ibtbeta` | Allows macOS newer than the declared maximum (`-ibtcompatbeta`, `-btlfxbeta`). |
| `-btlfxboardid` | Patches the board-id in `BlueTool`/`bluetoothd` (macOS 14+). |
| `-btlfxallowanyaddr` | Skips the "same address as the internal module" check (macOS 12.4+). |
| `-btlfxnvramcheck` | Skips the internal-controller NVRAM check (macOS 14 or earlier). |

### Reading the kernel logs

The kext's messages do not show up in `log show`; only in `sudo dmesg`. The default buffer (128 KB) fills up quickly; add the
boot-arg `msgbuf=1048576`. Look for `ibtp`, `btlfx` and `IntelFirmware`. You should see `ibtp: routed __ZN15IOUSBHostDevice13deviceRequest…`
and `btlfx: patched /usr/sbin/bluetoothd: …` lines.

## Building

You only need the **Command Line Tools** (no Xcode), `git`, `python3` and `make`.

```sh
make deps                          # clone MacKernelSDK and Lilu (commits pinned in the Makefile) into deps/
make FW="ibt-0041-*"               # AX210/AX211 firmware only → build/IntelBTUnified.kext
make clean; make                   # every firmware
make DEBUG=1                       # with DBGLOG
make dist                          # builds dist/IntelBTUnified-<version>-{AX210,full}.zip
```

Run `make clean` when you change `FW`. To pick another firmware, `FW` takes space-separated glob patterns
(`ls fw/` lists the files; the `ibt-XXXX-YYYY` name comes from the log: `Found device firmware …`).

## What changed from the originals

- **HCI patch (IntelBTPatcher):** it only acts on HCI commands from the Intel controllers in `devices.txt` (it used to inspect requests
  from *any* USB device); the random BLE address is generated per boot (it was a constant shared by every machine); lengths are
  validated (underflow in `wLength - 3`); atomic state; no descriptor leak; macOS ≤ 11 support removed.
- **`bluetoothd`/`BlueTool` patches (BlueToolFixup):** the same ones, and each logs a line the first time it is applied, so a pattern that
  stops matching after a macOS update shows up instead of failing silently.
- **Firmware driver:** a leaked `IOMemoryDescriptor` on every successful bulk write; `start()` error paths (PMstop, closing the device,
  the wrong `stop(this)` call); a leaked reference on `fw_name`; chip generation now taken from `devices.txt`; the uncompressed firmware size is
  stored when packaging instead of assuming 4×; its own `_start`/`_stop` removed, which overrode the ones from `libkmod`.
- **Packaging:** a single bundle and a generated `Info.plist`; reproducible builds without Xcode.

## Layout

```
devices.txt             chip table (single source of truth)
src/IntelBTUnified.cpp  Lilu plugin: HCI patch + bluetoothd/BlueTool patches
src/driver/             IOKit driver and Intel protocol (Gen1/2/3), derived from IntelBluetoothFirmware
fw/                     .sfi/.ddc firmware (Linux linux-firmware, license in licenses/)
scripts/                generators for Devices.h, Info.plist, FwBinary.cpp and kmod_info
Makefile                build with clang + MacKernelSDK + Lilu headers
```

## Credits and license

- [IntelBluetoothFirmware / IntelBTPatcher](https://github.com/OpenIntelWireless/IntelBluetoothFirmware) — zxystd and OpenIntelWireless (GPL‑3.0).
- [BlueToolFixup](https://github.com/acidanthera/BrcmPatchRAM) — Dhinak G and acidanthera.
- [Lilu](https://github.com/acidanthera/Lilu) and [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) — acidanthera.
- Intel firmware: redistributable under [`licenses/iwlwifi-firmware.txt`](licenses/iwlwifi-firmware.txt).

This project is distributed under **GPL‑3.0‑only** ([`LICENSE`](LICENSE)), inherited from the original projects.
Each source file keeps its original authorship header.

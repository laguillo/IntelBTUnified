# IntelBTUnified

**Un solo kext para el Bluetooth Intel en Hackintosh.** Sustituye a `IntelBluetoothFirmware` + `IntelBTPatcher` + `BlueToolFixup`
(plugin de Lilu + driver de carga de firmware en el mismo bundle). Solo macOS 12 (Monterey) o posterior, Intel/x86_64, OpenCore.

[English](README.md) · **Español**

## Estado

| | |
|---|---|
| Probado | **Intel AX210** (USB `8087:0032`), macOS 26.7 (Tahoe), Core i7‑6700T, OpenCore, Lilu 1.7.3. Carga el firmware, conecta y reproduce audio A2DP (auriculares clásicos). |
| Sin probar | El resto de chips de `devices.txt`, macOS 12 – 25, y dispositivos BLE (el parche de dirección aleatoria solo se nota con BLE). |
| Sin confirmar | Los mensajes de kernel de los parches (`ibtp`, `btlfx`): el buffer de `dmesg` los había perdido cuando se miró. |

Úsalo bajo tu responsabilidad: es código de kernel sin firmar. Conserva los kexts originales para poder volver atrás.

## Chips soportados

La lista sale de [`devices.txt`](devices.txt) y es la única fuente de verdad (genera la tabla del driver y las personalidades del `Info.plist`).
IDs de producto con vendor `0x8087`: `0025` `0026` `0029` `0032` `0033` `0035` `0036` `0038` `07dc` `0a2a` `0a2b` `0aa7` `0aaa`.
Un ID que no esté en la tabla se rechaza en vez de tratarse como Gen2 en silencio. Para añadir un chip: una línea en `devices.txt`
(y su firmware en `fw/` si hace falta) y recompilar. En la práctica, solo se ha ejercitado el camino de Gen3 (AX210).

## Instalación (OpenCore)

1. Descarga el zip de [Releases](../../releases) (o compílalo, ver abajo): `…-AX210.zip` lleva solo el firmware del AX210/AX211
   (≈0,6 MB); `…-full.zip` lleva todos los firmwares (≈11 MB).
2. Copia `IntelBTUnified.kext` a `EFI/OC/Kexts/`.
3. En `config.plist` → `Kernel → Add`: **quita `IntelBluetoothFirmware`, `IntelBTPatcher` y `BlueToolFixup`** y añade este **después de Lilu**.
   Cargarlos a la vez aplicaría los parches dos veces.
4. Reinicia y comprueba:
   - `ioreg -rc IntelBluetoothFirmware | grep fw_name` → `ibt-0041-0041.sfi` en un AX210 (`(already running)` si el chip conservó el
     firmware tras un reinicio sin cortar la alimentación; es normal).
   - `system_profiler SPBluetoothDataType` → `State: On`.
   - `ioreg -c IntelBTUnified -r` → el plugin de Lilu está activo (`VersionInfo`).

### Boot-args

| Argumento | Efecto |
|---|---|
| `-ibtoff` | Desactiva el plugin (parches). También valen `-ibtcompatoff` y `-btlfxoff`. |
| `-ibtdbg` | Log de depuración (`-ibtcompatdbg`, `-btlfxdbg`). |
| `-ibtbeta` | Permite macOS más nuevo que el máximo declarado (`-ibtcompatbeta`, `-btlfxbeta`). |
| `-btlfxboardid` | Parchea el board‑id en `BlueTool`/`bluetoothd` (macOS 14+). |
| `-btlfxallowanyaddr` | Salta el chequeo de «dirección igual a la del módulo interno» (macOS 12.4+). |
| `-btlfxnvramcheck` | Salta el chequeo de NVRAM del controlador interno (macOS 14 o anterior). |

### Ver los logs del kernel

Los mensajes del kext no salen en `log show`; solo en `sudo dmesg`. El buffer por defecto (128 KB) se llena enseguida; añade el
boot-arg `msgbuf=1048576`. Busca `ibtp`, `btlfx` e `IntelFirmware`. Deberías ver `ibtp: routed __ZN15IOUSBHostDevice13deviceRequest…`
y líneas `btlfx: patched /usr/sbin/bluetoothd: …`.

## Compilar

Solo hacen falta las **Command Line Tools** (sin Xcode), `git`, `python3` y `make`.

```sh
make deps                          # clona MacKernelSDK y Lilu (commits fijados en el Makefile) en deps/
make FW="ibt-0041-*"               # solo firmware AX210/AX211 → build/IntelBTUnified.kext
make clean; make                   # todos los firmwares
make DEBUG=1                       # con DBGLOG
make dist                          # genera dist/IntelBTUnified-<versión>-{AX210,full}.zip
```

Haz `make clean` al cambiar `FW`. Para elegir otro firmware, `FW` acepta patrones glob separados por espacios
(`ls fw/` muestra los ficheros; el nombre `ibt-XXXX-YYYY` sale del log: `Found device firmware …`).

## Qué cambia respecto a los originales

- **Parche HCI (IntelBTPatcher):** solo actúa sobre comandos HCI de controladores Intel de `devices.txt` (antes inspeccionaba las
  peticiones de *cualquier* dispositivo USB); la dirección aleatoria BLE se genera por arranque (antes era una constante igual en todas
  las máquinas); se validan longitudes (underflow en `wLength - 3`); estado atómico; sin fuga del descriptor; eliminado el soporte de macOS ≤ 11.
- **Parches de `bluetoothd`/`BlueTool` (BlueToolFixup):** los mismos, y cada uno deja una línea en el log la primera vez que se aplica,
  de modo que un patrón que deje de coincidir tras actualizar macOS se nota en lugar de fallar en silencio.
- **Driver de firmware:** fuga de un `IOMemoryDescriptor` en cada escritura bulk correcta; caminos de error de `start()` (PMstop, cierre
  del dispositivo, `stop(this)` incorrecto); referencia perdida en `fw_name`; generación del chip según `devices.txt`; el tamaño del
  firmware descomprimido se guarda al empaquetar en vez de suponer 4×; se quita el `_start`/`_stop` propio, que pisaba los de `libkmod`.
- **Empaquetado:** un solo bundle y un solo `Info.plist` generado; compilación reproducible sin Xcode.

## Estructura

```
devices.txt           tabla de chips (fuente de verdad)
src/IntelBTUnified.cpp  plugin de Lilu: parche HCI + parches de bluetoothd/BlueTool
src/driver/           driver IOKit y protocolo Intel (Gen1/2/3), derivado de IntelBluetoothFirmware
fw/                   firmwares .sfi/.ddc (Linux linux-firmware, licencia en licenses/)
scripts/              generadores de Devices.h, Info.plist, FwBinary.cpp y kmod_info
Makefile              compilación con clang + MacKernelSDK + cabeceras de Lilu
```

## Créditos y licencia

- [IntelBluetoothFirmware / IntelBTPatcher](https://github.com/OpenIntelWireless/IntelBluetoothFirmware) — zxystd y OpenIntelWireless (GPL‑3.0).
- [BlueToolFixup](https://github.com/acidanthera/BrcmPatchRAM) — Dhinak G y acidanthera.
- [Lilu](https://github.com/acidanthera/Lilu) y [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) — acidanthera.
- Firmwares Intel: redistribuibles bajo [`licenses/iwlwifi-firmware.txt`](licenses/iwlwifi-firmware.txt).

Este proyecto se distribuye bajo **GPL‑3.0‑only** ([`LICENSE`](LICENSE)), heredada de los proyectos originales.
Cada fichero fuente conserva su cabecera de autoría original.

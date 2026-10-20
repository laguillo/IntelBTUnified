#!/usr/bin/python3
# Genera Info.plist a partir de devices.txt. uso: gen_plist.py devices.txt salida.plist version
import plistlib, sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from gen_devices import load

devs, out, version = load(sys.argv[1]), sys.argv[2], sys.argv[3]
BID = 'local.IntelBTUnified'
pers = {}
for pid, gen, name in devs:
    pers['IntelBluetoothFirmware_%04X' % pid] = {
        'CFBundleIdentifier': BID,
        'IOClass': 'IntelBluetoothFirmware',
        'IOMatchCategory': 'IntelBluetoothFirmware',
        'IOProbeScore': 4000,
        'IOProviderClass': 'IOUSBHostDevice',
        'idProduct': pid,
        'idVendor': 0x8087,
    }
pers['IntelBTUnified'] = {
    'CFBundleIdentifier': BID,
    'IOClass': 'IntelBTUnified',
    'IOMatchCategory': 'IntelBTUnified',
    'IOProviderClass': 'IOResources',
    'IOResourceMatch': 'IOKit',
}
plist = {
    'CFBundleDevelopmentRegion': 'en',
    'CFBundleExecutable': 'IntelBTUnified',
    'CFBundleIdentifier': BID,
    'CFBundleInfoDictionaryVersion': '6.0',
    'CFBundleName': 'IntelBTUnified',
    'CFBundlePackageType': 'KEXT',
    'CFBundleShortVersionString': version,
    'CFBundleVersion': version,
    'IOKitPersonalities': pers,
    'LSMinimumSystemVersion': '12.0',
    'NSHumanReadableCopyright': 'GPL-3.0-only. Basado en IntelBluetoothFirmware/IntelBTPatcher (zxystd, OpenIntelWireless) y BlueToolFixup (acidanthera).',
    'OSBundleRequired': 'Root',
    'OSBundleLibraries': {
        'as.vit9696.Lilu': '1.4.7',
        'com.apple.iokit.IOUSBHostFamily': '1.2',
        'com.apple.kpi.bsd': '12.0.0',
        'com.apple.kpi.dsep': '12.0.0',
        'com.apple.kpi.iokit': '12.0.0',
        'com.apple.kpi.libkern': '12.0.0',
        'com.apple.kpi.mach': '12.0.0',
        'com.apple.kpi.unsupported': '12.0.0',
    },
}
with open(out, 'wb') as f:
    plistlib.dump(plist, f)

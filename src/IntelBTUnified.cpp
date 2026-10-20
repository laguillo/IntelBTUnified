//
//  IntelBTUnified.cpp
//
//  Plugin de Lilu que junta en un solo kext:
//    - IntelBTPatcher (zxystd, OpenIntelWireless): direccion aleatoria BLE para controladores Intel.
//    - BlueToolFixup (Dhinak G / acidanthera): parches de BlueTool y bluetoothd en memoria.
//  El driver que carga el firmware (IntelBluetoothFirmware) vive en el mismo bundle.
//
//  Solo macOS 12 (Monterey) o posterior: los parches de macOS 11 y anteriores se han eliminado.
//
//  SPDX-License-Identifier: GPL-3.0-only
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_user.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_version.hpp>
#include <Headers/kern_devinfo.hpp>

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <sys/random.h>

#include "Devices.h"

#define MODULE_IBTP  "ibtp"
#define MODULE_BTLFX "btlfx"

#pragma mark - Intel HCI: direccion aleatoria (antes IntelBTPatcher)

#define INTEL_VENDOR_ID             0x8087
#define HCI_OP_RESET                0x0c03
#define HCI_OP_LE_SET_SCAN_PARAM    0x200B
#define HCI_OP_LE_SET_RANDOM_ADDR   0x2005
#define HCI_CMD_HDR_SIZE            3
#define MAX_HCI_BUF_LEN             255

typedef struct __attribute__((packed)) {
    uint16_t opcode;
    uint8_t  len;
    uint8_t  data[];
} HciCommandHdr;

static mach_vm_address_t oldHostDeviceRequest {};

// El original usaba una direccion constante igual para todo el mundo. Ahora se genera
// una por arranque. Una direccion estatica aleatoria BLE exige los dos bits altos a 1.
static uint8_t randomAddrCmd[HCI_CMD_HDR_SIZE + 6];
// 1 = ya enviada tras el ultimo HCI_Reset. Se toca desde un hook que puede ejecutarse en paralelo.
static int randomAddrSent = 0;

static void makeRandomAddressCommand() {
    uint8_t addr[6];
    bool allZero, allOne;
    do {
        read_random(addr, sizeof(addr));
        addr[5] |= 0xC0;
        // La parte aleatoria (46 bits) no puede ser todo ceros ni todo unos.
        allZero = (addr[0] | addr[1] | addr[2] | addr[3] | addr[4]) == 0 && (addr[5] & 0x3F) == 0;
        allOne  = (addr[0] & addr[1] & addr[2] & addr[3] & addr[4]) == 0xFF && (addr[5] & 0x3F) == 0x3F;
    } while (allZero || allOne);
    randomAddrCmd[0] = HCI_OP_LE_SET_RANDOM_ADDR & 0xFF;
    randomAddrCmd[1] = HCI_OP_LE_SET_RANDOM_ADDR >> 8;
    randomAddrCmd[2] = 6;
    memcpy(&randomAddrCmd[HCI_CMD_HDR_SIZE], addr, sizeof(addr));
}

static bool isIntelBluetooth(IOUSBHostDevice *dev) {
    auto desc = dev ? dev->getDeviceDescriptor() : nullptr;
    if (!desc || USBToHost16(desc->idVendor) != INTEL_VENDOR_ID)
        return false;
    uint16_t pid = USBToHost16(desc->idProduct);
    for (const IbtDevice &d : kIbtDevices)
        if (d.pid == pid)
            return true;
    return false;
}

static IOReturn newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor, unsigned int &length, IOUSBHostCompletion *completion, unsigned int timeout);

static void sendRandomAddress(void *that, IOService *provider, unsigned int timeout) {
    StandardUSB::DeviceRequest req {};
    req.bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionOut, kRequestTypeClass, kRequestRecipientInterface);
    req.bRequest = 0xE0;
    req.wValue = 0;
    req.wIndex = 0;
    req.wLength = sizeof(randomAddrCmd);
    unsigned int len = sizeof(randomAddrCmd);

    IOReturn ret = kIOReturnNoMemory;
    auto desc = IOBufferMemoryDescriptor::withBytes(randomAddrCmd, sizeof(randomAddrCmd), kIODirectionOut);
    if (desc) {
        ret = desc->prepare(kIODirectionOut);
        if (ret == kIOReturnSuccess) {
            ret = FunctionCast(newHostDeviceRequest, oldHostDeviceRequest)(that, provider, req, nullptr, desc, len, nullptr, timeout);
            desc->complete(kIODirectionOut);
        }
        desc->release();
    }
    SYSLOG(MODULE_IBTP, "[PATCH] Sending random address HCI: 0x%x", ret);
    if (ret != kIOReturnSuccess)
        __atomic_store_n(&randomAddrSent, 0, __ATOMIC_SEQ_CST);  // reintentar en el siguiente scan
}

static IOReturn newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor, unsigned int &length, IOUSBHostCompletion *completion, unsigned int timeout) {
    // Antes se inspeccionaba cualquier peticion de cualquier dispositivo USB. Ahora solo comandos
    // HCI (clase, host->dispositivo, bRequest 0) de un controlador Bluetooth Intel conocido.
    auto dev = OSDynamicCast(IOUSBHostDevice, reinterpret_cast<OSObject *>(that));
    if (dev && request.wLength >= HCI_CMD_HDR_SIZE && request.bRequest == 0 &&
        (request.bmRequestType & kDeviceRequestDirectionMask) == (kRequestDirectionOut << kDeviceRequestDirectionPhase) &&
        (request.bmRequestType & kDeviceRequestTypeMask) == (kRequestTypeClass << kDeviceRequestTypePhase) &&
        isIntelBluetooth(dev)) {
        uint8_t hciBuf[MAX_HCI_BUF_LEN] = {};
        const HciCommandHdr *hdr = nullptr;
        if (data != nullptr) {
            hdr = static_cast<const HciCommandHdr *>(data);
        } else if (descriptor != nullptr && descriptor->getLength() >= HCI_CMD_HDR_SIZE) {
            // En Sequoia+ el descriptor llega sin preparar y no se puede leer directamente.
            bool prepared = getKernelVersion() < KernelVersion::Sequoia || descriptor->prepare(kIODirectionOut) == kIOReturnSuccess;
            if (prepared) {
                descriptor->readBytes(0, hciBuf, min(descriptor->getLength(), (IOByteCount)MAX_HCI_BUF_LEN));
                if (getKernelVersion() >= KernelVersion::Sequoia)
                    descriptor->complete(kIODirectionOut);
                hdr = reinterpret_cast<const HciCommandHdr *>(hciBuf);
            }
        }
        if (hdr) {
            if (hdr->opcode == HCI_OP_RESET) {
                // Tras un reset hay que volver a enviar la direccion.
                __atomic_store_n(&randomAddrSent, 0, __ATOMIC_SEQ_CST);
            } else if (hdr->opcode == HCI_OP_LE_SET_SCAN_PARAM) {
                if (__atomic_exchange_n(&randomAddrSent, 1, __ATOMIC_SEQ_CST) == 0)
                    sendRandomAddress(that, provider, timeout);
            }
            DBGLOG(MODULE_IBTP, "opcode: 0x%04x len: %u wLength: %u async: %d", hdr->opcode, hdr->len, request.wLength, completion != nullptr);
        }
    }
    return FunctionCast(newHostDeviceRequest, oldHostDeviceRequest)(that, provider, request, data, descriptor, length, completion, timeout);
}

static const char *kextIOUSBHostPath[] { "/System/Library/Extensions/IOUSBHostFamily.kext/Contents/MacOS/IOUSBHostFamily" };

static KernelPatcher::KextInfo kextIOUSBHost {
    "com.apple.iokit.IOUSBHostFamily",
    kextIOUSBHostPath,
    1,
    {true, true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

static void processUSBHost(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (kextIOUSBHost.loadIndex != index)
        return;
    makeRandomAddressCommand();
    KernelPatcher::RouteRequest request {
        "__ZN15IOUSBHostDevice13deviceRequestEP9IOServiceRN11StandardUSB13DeviceRequestEPvP18IOMemoryDescriptorRjP19IOUSBHostCompletionj",
        newHostDeviceRequest,
        oldHostDeviceRequest
    };
    patcher.routeMultiple(index, &request, 1, address, size);
    if (patcher.getError() == KernelPatcher::Error::NoError) {
        SYSLOG(MODULE_IBTP, "routed %s", request.symbol);
    } else {
        SYSLOG(MODULE_IBTP, "failed to resolve %s, error = %d", request.symbol, patcher.getError());
        patcher.clearError();
    }
}

#pragma mark - BlueTool / bluetoothd (antes BlueToolFixup)

static const uint8_t kSkipUpdateFilePathOriginal[] = "/etc/bluetool/SkipBluetoothAutomaticFirmwareUpdate";
static const uint8_t kSkipUpdateFilePathPatched[]  = "/System/Library/CoreServices/boot.efi";

// Workaround 12.4 Beta 3+: macOS puede detectar el chipset dos veces (interno y dongle externo):
// 'ERROR -- Third Party Dongle has the same address as the internal module'
static const uint8_t kSkipAddressCheckOriginal[] = {
    0x48, 0x89, 0xF3,             // mov    rbx, rsi
    0xE8, 0x00, 0x00, 0x00, 0x00, // call   <somewhere>
    0x85, 0xC0,                   // test   eax, eax
    0x74, 0x1D,                   // je
};
static const uint8_t kSkipAddressCheckPatched[] = {
    0x48, 0x89, 0xF3,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x85, 0xC0,
    0x72, 0x1D,                   // jb short
};
static const uint8_t kSkipAddressCheckMask[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
};

static const uint8_t kVendorCheckOriginal[] = { 0x81, 0xFA, 0x5C, 0x0A, 0x00, 0x00, 0x74 };  // cmp edx, BRCM; je
static const uint8_t kVendorCheckPatched[]  = { 0x81, 0xFA, 0x5C, 0x0A, 0x00, 0x00, 0xEB };  // jmp

// El chequeo de rango de chipset no considera valido el 0 "THIRD_PARTY_DONGLE": sin esto el
// Bluetooth no vuelve a encenderse tras el primer ciclo de energia (acidanthera/BrcmPatchRAM#18).
static const uint8_t kBadChipsetCheckOriginal[] = { 0x81, 0xF9, 0xCF, 0x07, 0x00, 0x00, 0x72 };  // jb
static const uint8_t kBadChipsetCheckPatched[]  = { 0x81, 0xF9, 0xCF, 0x07, 0x00, 0x00, 0xEB };  // jmp

static const uint8_t kBadChipsetCheckOriginal13_3[] = { 0x81, 0xF9, 0x9E, 0x0F, 0x00, 0x00, 0x77, 0x1A };
static const uint8_t kBadChipsetCheckPatched13_3[]  = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

static const uint8_t kBadChipsetCheckOriginal15_4[] = { 0x81, 0xF9, 0x6F, 0x17, 0x00, 0x00, 0x77, 0x30 };
static const uint8_t kBadChipsetCheckPatched15_4[]  = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

static const uint8_t kSkipInternalControllerNVRAMCheck13_3[] = {
    0x41, 0x80, 0x00, 0x01, // xor     r15b, 1
    0x75, 0x00,             // jnz     short
    0x84, 0xDB,             // test    bl, bl
    0x75, 0x00              // jnz     short
};
static const uint8_t kSkipInternalControllerNVRAMCheckMask13_3[] = {
    0xFF, 0xFF, 0x00, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF,
    0xFF, 0x00
};
static const uint8_t kSkipInternalControllerNVRAMCheckPatched13_3[] = {
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};

static bool shouldPatchBoardId = false;
static bool shouldPatchAddress = false;
static bool shouldPatchNvramCheck = false;

static constexpr size_t kBoardIdSize = sizeof("Mac-F60DEB81FF30ACF6");
static constexpr size_t kBoardIdSizeLegacy = sizeof("Mac-F22586C8");

static const char boardIdsWithUSBBluetooth[][kBoardIdSize] = {
    "Mac-F60DEB81FF30ACF6", "Mac-9F18E312C5C2BF0B", "Mac-937CB26E2E02BB01", "Mac-E43C1C25D4880AD6",
    "Mac-06F11FD93F0323C5", "Mac-06F11F11946D27C5", "Mac-A369DDC4E67F1C45", "Mac-FFE5EF870D7BA81A",
    "Mac-DB15BD556843C820", "Mac-B809C3757DA9BB8D", "Mac-65CE76090165799A", "Mac-4B682C642B45593E",
    "Mac-77F17D7DA9285301", "Mac-BE088AF8C5EB4FA2"
};

static mach_vm_address_t orig_cs_validate {};

// Cada parche deja constancia la primera vez que se aplica. Si tras una actualizacion de macOS
// un patron deja de coincidir, el parche desaparece del log en vez de fallar en silencio.
enum PatchId : uint32_t {
    PatchSkipUpdate  = 1u << 0,
    PatchBlueToolBid = 1u << 1,
    PatchVendor      = 1u << 2,
    PatchChipset     = 1u << 3,
    PatchChipset13   = 1u << 4,
    PatchChipset15   = 1u << 5,
    PatchNvram       = 1u << 6,
    PatchBtdBid      = 1u << 7,
    PatchAddress     = 1u << 8,
};
static uint32_t appliedPatches = 0;

static inline void notePatch(PatchId id, const char *name, const char *path) {
    if ((__atomic_fetch_or(&appliedPatches, (uint32_t)id, __ATOMIC_RELAXED) & id) == 0)
        SYSLOG(MODULE_BTLFX, "patched %s: %s", path, name);
}

static inline void searchAndPatch(PatchId id, const char *name, const void *haystack, size_t haystackSize, const char *path, const void *needle, size_t findSize, const void *patch, size_t replaceSize) {
    if (KernelPatcher::findAndReplace(const_cast<void *>(haystack), haystackSize, needle, findSize, patch, replaceSize))
        notePatch(id, name, path);
}

template <size_t findSize, size_t replaceSize>
static inline void searchAndPatch(PatchId id, const char *name, const void *haystack, size_t haystackSize, const char *path, const uint8_t (&needle)[findSize], const uint8_t (&patch)[replaceSize]) {
    searchAndPatch(id, name, haystack, haystackSize, path, needle, findSize, patch, replaceSize);
}

static inline void searchAndPatchWithMask(PatchId id, const char *name, const void *haystack, size_t haystackSize, const char *path, const void *needle, size_t findSize, const void *findMask, size_t findMaskSize, const void *patch, size_t replaceSize) {
    if (KernelPatcher::findAndReplaceWithMask(const_cast<void *>(haystack), haystackSize, needle, findSize, findMask, findMaskSize, patch, replaceSize, nullptr, 0))
        notePatch(id, name, path);
}

static void patched_cs_validate_page(vnode_t vp, memory_object_t pager, memory_object_offset_t page_offset, const void *data, int *validated_p, int *tainted_p, int *nx_p) {
    char path[PATH_MAX];
    int pathlen = PATH_MAX;
    FunctionCast(patched_cs_validate_page, orig_cs_validate)(vp, pager, page_offset, data, validated_p, tainted_p, nx_p);
    static constexpr size_t dirLength = sizeof("/usr/sbin/")-1;
    if (vn_getpath(vp, path, &pathlen) == 0 && UNLIKELY(strncmp(path, "/usr/sbin/", dirLength) == 0)) {
        if (strcmp(path + dirLength, "BlueTool") == 0) {
            searchAndPatch(PatchSkipUpdate, "skip firmware auto-update", data, PAGE_SIZE, path, kSkipUpdateFilePathOriginal, kSkipUpdateFilePathPatched);
            if (shouldPatchBoardId)
                searchAndPatch(PatchBlueToolBid, "board-id", data, PAGE_SIZE, path, boardIdsWithUSBBluetooth[0], kBoardIdSize, BaseDeviceInfo::get().boardIdentifier, kBoardIdSize);
        }
        else if (strcmp(path + dirLength, "bluetoothd") == 0) {
            searchAndPatch(PatchVendor, "vendor check", data, PAGE_SIZE, path, kVendorCheckOriginal, kVendorCheckPatched);
            searchAndPatch(PatchChipset, "chipset range check", data, PAGE_SIZE, path, kBadChipsetCheckOriginal, kBadChipsetCheckPatched);
            if (getKernelVersion() >= KernelVersion::Ventura && getKernelVersion() < KernelVersion::Sequoia)
                searchAndPatch(PatchChipset13, "chipset range check (13.3)", data, PAGE_SIZE, path, kBadChipsetCheckOriginal13_3, kBadChipsetCheckPatched13_3);
            if (getKernelVersion() >= KernelVersion::Sequoia)
                searchAndPatch(PatchChipset15, "chipset range check (15.4)", data, PAGE_SIZE, path, kBadChipsetCheckOriginal15_4, kBadChipsetCheckPatched15_4);
            if (shouldPatchNvramCheck)
                searchAndPatchWithMask(PatchNvram, "internal controller NVRAM check", data, PAGE_SIZE, path, kSkipInternalControllerNVRAMCheck13_3, sizeof(kSkipInternalControllerNVRAMCheck13_3), kSkipInternalControllerNVRAMCheckMask13_3, sizeof(kSkipInternalControllerNVRAMCheckMask13_3), kSkipInternalControllerNVRAMCheckPatched13_3, sizeof(kSkipInternalControllerNVRAMCheckPatched13_3));
            if (shouldPatchBoardId)
                searchAndPatch(PatchBtdBid, "board-id", data, PAGE_SIZE, path, boardIdsWithUSBBluetooth[0], kBoardIdSize, BaseDeviceInfo::get().boardIdentifier, kBoardIdSize);
            if (shouldPatchAddress)
                searchAndPatchWithMask(PatchAddress, "same-address check", data, PAGE_SIZE, path, kSkipAddressCheckOriginal, sizeof(kSkipAddressCheckOriginal), kSkipAddressCheckMask, sizeof(kSkipAddressCheckMask), kSkipAddressCheckPatched, sizeof(kSkipAddressCheckPatched));
        }
    }
}

static void processPatcher(KernelPatcher &patcher) {
    if (getKernelVersion() >= KernelVersion::Sonoma) {
        shouldPatchBoardId = checkKernelArgument("-btlfxboardid");
    } else {
        auto boardId = BaseDeviceInfo::get().boardIdentifier;
        auto boardIdSize = strlen(boardId) + 1;
        shouldPatchBoardId = boardIdSize == kBoardIdSize || boardIdSize == kBoardIdSizeLegacy;
        if (shouldPatchBoardId)
            for (size_t i = 0; i < arrsize(boardIdsWithUSBBluetooth); i++)
                if (strcmp(boardIdsWithUSBBluetooth[i], boardId) == 0) {
                    shouldPatchBoardId = false;
                    break;
                }
    }
    // 12.4 Beta 3+, XNU 21.5
    if ((getKernelVersion() == KernelVersion::Monterey && getKernelMinorVersion() >= 5) || getKernelVersion() > KernelVersion::Monterey)
        shouldPatchAddress = checkKernelArgument("-btlfxallowanyaddr");
    if (getKernelVersion() <= KernelVersion::Sonoma)
        shouldPatchNvramCheck = checkKernelArgument("-btlfxnvramcheck");
    KernelPatcher::RouteRequest csRoute("_cs_validate_page", patched_cs_validate_page, orig_cs_validate);
    if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &csRoute, 1))
        SYSLOG(MODULE_BTLFX, "failed to route cs validation pages");
}

#pragma mark - Lilu

static void pluginStart() {
    SYSLOG("ibt", "start");
    lilu.onKextLoadForce(&kextIOUSBHost, 1,
        [](void *, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            processUSBHost(patcher, index, address, size);
        });
    lilu.onPatcherLoadForce([](void *, KernelPatcher &patcher) {
        processPatcher(patcher);
    });
}

// Argumentos de arranque. Se aceptan tambien los de los kexts originales.
static const char *bootargOff[] { "-ibtoff", "-ibtcompatoff", "-btlfxoff" };
static const char *bootargDebug[] { "-ibtdbg", "-ibtcompatdbg", "-btlfxdbg" };
static const char *bootargBeta[] { "-ibtbeta", "-ibtcompatbeta", "-btlfxbeta" };

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::Monterey,
    KernelVersion::Tahoe,
    pluginStart
};

//
//  IntelBluetoothFirmware.cpp
//  IntelBluetoothFirmware
//
//  Created by zxystd on 2019/11/17.
//  Copyright © 2019 zxystd. All rights reserved.
//

#include "BtIntel.h"

#include "IntelBluetoothFirmware.hpp"
#include <libkern/libkern.h>
#include <libkern/OSKextLib.h>
#include <libkern/version.h>
#include <libkern/OSTypes.h>
#include <IOKit/usb/StandardUSB.h>
#include "Hci.h"
#include "linux.h"
#include "Log.h"
#include "Devices.h"

#include "IntelBluetoothOpsGen1.hpp"
#include "IntelBluetoothOpsGen2.hpp"
#include "IntelBluetoothOpsGen3.hpp"

#define super IOService
OSDefineMetaClassAndStructors(IntelBluetoothFirmware, IOService)

//com.apple.iokit.IOBluetoothHostControllerUSBTransport

enum { kMyOffPowerState = 0, kMyOnPowerState = 1 };

#define kIOPMPowerOff 0

static IOPMPowerState myTwoStates[2] =
{
    {1, kIOPMPowerOff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

bool IntelBluetoothFirmware::init(OSDictionary *dictionary)
{
    XYLog("Driver init()\n");
    return super::init(dictionary);
}

void IntelBluetoothFirmware::free() {
    XYLog("Driver free()\n");
    super::free();
}

// Camino de error comun de start(): IOKit solo llama a stop() si start() tuvo exito,
// asi que aqui hay que deshacer PMinit y cerrar el dispositivo nosotros.
bool IntelBluetoothFirmware::fail()
{
    cleanUp();
    if (m_pmStarted) {
        PMstop();
        m_pmStarted = false;
    }
    return false;
}

bool IntelBluetoothFirmware::start(IOService *provider)
{
    XYLog("Driver Start()\n");
    char fwName[64];
    m_pDevice = OSDynamicCast(IOUSBHostDevice, provider);
    if (m_pDevice == NULL) {
        XYLog("Driver Start fail, not usb device\n");
        return false;
    }
    PMinit();
    m_pmStarted = true;
    registerPowerDriver(this, myTwoStates, 2);
    provider->joinPMtree(this);
    makeUsable();
    
    if (!super::start(provider))
        return fail();
    
    if (!m_pDevice->open(this)) {
        XYLog("start fail, can not open device\n");
        return fail();
    }
    if (currentType == kTypeGen1) {
        m_pBTIntel = new IntelBluetoothOpsGen1();
    } else {
        m_pBTIntel = new IntelBluetoothOpsGen3();
    }
    if (!m_pBTIntel || !m_pBTIntel->initWithDevice(this, m_pDevice)) {
        XYLog("start fail, can not init device\n");
        return fail();
    }
    XYLog("BT init succeed\n");
    if (!m_pBTIntel->setup()) {
        m_pDevice->setProperty("FirmwareLoaded", false);
        return fail();
    }
    m_pBTIntel->getFirmwareName(fwName, sizeof(fwName));
    publishReg(true, fwName);
    cleanUp();
    return true;
}

void IntelBluetoothFirmware::publishReg(bool isSucceed, const char *fwName)
{
    m_pDevice->setProperty("FirmwareLoaded", isSucceed);
    // Tras un reinicio sin cortar la alimentacion el chip conserva el firmware y no se descarga
    // nada: el nombre queda vacio.
    if (isSucceed)
        setProperty("fw_name", fwName[0] ? fwName : "(already running)");
    // Monterey+
    if (version_major >= 21)
        m_pDevice->setName("Bluetooth USB Host Controller");
}

void IntelBluetoothFirmware::cleanUp()
{
    XYLog("Clean up...\n");
    OSSafeReleaseNULL(m_pBTIntel);
    if (m_pDevice) {
        if (m_pDevice->isOpen(this)) {
            m_pDevice->close(this);
        }
        m_pDevice = NULL;
    }
}

IOReturn IntelBluetoothFirmware::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice)
{
    return IOPMAckImplied;
}

void IntelBluetoothFirmware::stop(IOService *provider)
{
    XYLog("Driver Stop()\n");
    if (m_pmStarted) {
        PMstop();
        m_pmStarted = false;
    }
    super::stop(provider);
}

IOService * IntelBluetoothFirmware::probe(IOService *provider, SInt32 *score)
{
    XYLog("Driver Probe()\n");
    if (!super::probe(provider, score)) {
        XYLog("super probe failed\n");
        return NULL;
    }
    IOUSBHostDevice *dev = OSDynamicCast(IOUSBHostDevice, provider);
    if (!dev) {
        XYLog("is not usb device\n");
        return NULL;
    }
    UInt16 vendorID = USBToHost16(dev->getDeviceDescriptor()->idVendor);
    UInt16 productID = USBToHost16(dev->getDeviceDescriptor()->idProduct);
    XYLog("name=%s, class=%s, vendorID=0x%04X, productID=0x%04X\n", dev->getName(), provider->metaClass->getClassName(), vendorID, productID);
    // La generacion sale de devices.txt (la misma tabla que genera las personalidades
    // del Info.plist). Un ID desconocido ya no cae en Gen2 en silencio: se rechaza.
    for (const IbtDevice &d : kIbtDevices) {
        if (d.pid == productID && vendorID == 0x8087) {
            currentType = (BTType)d.gen;
            return this;
        }
    }
    XYLog("productID 0x%04X not in the supported device table\n", productID);
    return NULL;
}

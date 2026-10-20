/** @file
  Copyright (c) 2020 zxystd. All rights reserved.
  SPDX-License-Identifier: GPL-3.0-only
**/

//
//  IntelBluetoothFirmware.hpp
//  IntelBluetoothFirmware
//
//  Created by zxystd on 2019/11/17.
//  Copyright © 2019 zxystd. All rights reserved.
//

#ifndef IntelBluetoothFirmware_H
#define IntelBluetoothFirmware_H

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/usb/USB.h>
#include <libkern/OSKextLib.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>

#include "BtIntel.h"

enum BTType {
    kTypeGen1 = 1,
    kTypeGen2 = 2,
    kTypeGen3 = 3,
};

class IntelBluetoothFirmware : public IOService
{
    OSDeclareDefaultStructors (IntelBluetoothFirmware)
    
public:
    
    bool init(OSDictionary *dictionary = NULL) override;
    
    void free() override;
    
    bool start(IOService *provider) override;
    
    void stop(IOService *provider) override;
    
    IOService * probe(IOService *provider, SInt32 *score) override;
    
    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) override;
    
    void cleanUp();
    
    void publishReg(bool isSucceed, const char *fwName);
    
private:
    bool fail();
    BTType currentType {kTypeGen2};
    BtIntel *m_pBTIntel {nullptr};
    IOUSBHostDevice* m_pDevice {nullptr};
    bool m_pmStarted {false};
};

#endif

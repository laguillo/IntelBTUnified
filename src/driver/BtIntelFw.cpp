//
//  BtIntelFw.cpp
//  IntelBluetoothFirmware
//

#include "Log.h"
#include "BtIntel.h"
#include "FwData.h"

OSData *BtIntel::
requestFirmwareData(const char *fwName, bool noWarn)
{
    const FwDesc *desc = findFwByName(fwName);
    if (!desc) {
        if (!noWarn)
            XYLog("Firmware: %s Not found!\n", fwName);
        return NULL;
    }
    XYLog("Found device firmware %s \n", fwName);
    // El tamano real se guarda al empaquetar: ya no se estima como 4x el comprimido.
    uint rawSize = (uint)desc->rawSize;
    unsigned char *buf = (unsigned char *)IOMalloc(rawSize);
    if (!buf)
        return NULL;
    uint actual = rawSize;
    OSData *fwData = NULL;
    if (uncompressFirmware(buf, &actual, (unsigned char *)desc->data, (uint)desc->size))
        fwData = OSData::withBytes(buf, actual);
    IOFree(buf, rawSize);
    if (fwData == NULL)
        XYLog("Firmware %s uncompress fail!\n", fwName);
    return fwData;
}

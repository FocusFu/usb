import usb.util
import sys
import usb.core

import time





if __name__ == "__main__":
    dev = usb.core.find(idVendor=0x2309, idProduct=0x0606)
    cfg = dev.get_active_configuration()
    intf = cfg[(0, 0)]
    ep = intf[0]
    string = 0x105100000A
    dev.set_configuration()
    #print(dev)

    ep = dev[0][0, 0][0]
    dev.write(1, Q)
    #a = ep.read(ep.wMaxPacketSize, timeout=5000)
    #print('1')
    #bytes_writted = ep.write(string)
    buffer = dev.read(1, 10)
    print(buffer)







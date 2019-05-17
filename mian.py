
import usb.util
import sys
all_devs = usb.core.find(find_all=True)
vid=0x2309
pid=0x0606
for d in all_devs:
    if (d.idVendor == vid) & (d.idProduct == pid):
        print(d)

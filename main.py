import os
import subprocess
from ctypes import *
import sys





if __name__ == "__main__":
    current_dir = os.path.abspath(os.path.dirname(__file__))
    print(current_dir)
    sys.path.append(current_dir)
    sys.path.append(current_dir+'\libusb-1.0')
    dll = cdll.LoadLibrary('G:\\mycode\\usb\\usbdll.dll')
    z = dll.main()








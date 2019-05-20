from ctypes import *
if __name__ == "__main__":
    dll = cdll.LoadLibrary('G:\\mycode\\usb\\usbdll.dll')
    z = dll.main()








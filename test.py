#!/usr/bin/python
# -*- coding:utf-8 -*-

import usb.core
import usb.util
import sys
import time

# from tools import*

dev = usb.core.find(idVendor=0x2309, idProduct=0x0606)

dev.set_configuration()
ep = dev[0][(0, 0)][0]
out_ep_address = ep.bEndpointAddress

# print("ep.bEndpointAddress = %s" %out_ep_address)
dev.write(0x01, [0x55, 0x53, 0x42, 0x43,  # dCBWSignature
                 0x00, 0x00, 0x00, 0x01,  # dCBWTag
                 0x00, 0x00, 0x00, 0x10,  # dataTransferLength 16
                 0x00,  # CBWFlags 0x00 RECV HOST, 0x80 SEND HOST
                 0x00,  # CBWLUN
                 0x06,  # CBWLength  cdb_len = 6
                 0xfe, 0x10, 0x51, 0x00, 0x00, 0x0A,  # cdb 0x20,0xa0,0x00,0x02,0x00
                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])  # total 31

data = dev.read(0x82, ep.wMaxPacketSize)
dev.reset()
hex_array = []

for item in data:
    hex_array.append('%02x' % item)
print(hex_array)
time.sleep(0.5)

dev = usb.core.find(idVendor=0x2309, idProduct=0x0606)
dev.set_configuration()
ep = dev[0][(0, 0)][0]
out_ep_address = ep.bEndpointAddress

# print("ep.bEndpointAddress = %s" %out_ep_address)

dev.write(0x01, [0x55, 0x53, 0x42, 0x43,  # dCBWSignature
                 0x00, 0x00, 0x00, 0x01,  # dCBWTag
                 0x00, 0x00, 0x00, 0x10,  # dataTransferLength 16
                 0x00,  # CBWFlags 0x00 RECV HOST, 0x80 SEND HOST
                 0x00,  # CBWLUN
                 0x06,  # CBWLength  cdb_len = 6
                 0xfe, 0x00, 0x51, 0x00, 0x00, 0x0A,  # cdb 0x20,0xa0,0x00,0x02,0x00
                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])  # total 31

data = dev.read(0x82, ep.wMaxPacketSize)
dev.reset()
hex_array = []
for item in data:
    hex_array.append('%02x' % item)
print(hex_array)
time.sleep(0.5)

dev = usb.core.find(idVendor=0x2309, idProduct=0x0606)

dev.set_configuration()
ep = dev[0][(0, 0)][0]
out_ep_address = ep.bEndpointAddress

# print("ep.bEndpointAddress = %s" %out_ep_address)

dev.write(0x01, [0x55, 0x53, 0x42, 0x43,  # dCBWSignature
                 0x00, 0x00, 0x00, 0x01,  # dCBWTag
                 0x00, 0x00, 0x00, 0x10,  # dataTransferLength 16
                 0x00,  # CBWFlags 0x00 RECV HOST, 0x80 SEND HOST
                 0x00,  # CBWLUN
                 0x06,  # CBWLength  cdb_len = 6
                 0xfe, 0x00, 0x51, 0x00, 0x00, 0x0A,  # cdb 0x20,0xa0,0x00,0x02,0x00
                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])  # total 31

data = dev.read(0x82, ep.wMaxPacketSize)
dev.reset()
hex_array = []
for item in data:
    hex_array.append('%02x' % item)
print(hex_array)
time.sleep(0.5)
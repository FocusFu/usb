#!/usr/bin/python
# -*- coding:utf-8 -*-
print('hello')

import os
from time import sleep


# 获取设备id列表
def getdevlist():
    devlist = []
    connectfile = os.popen('adb devices')
    list = connectfile.readlines()
    # print(list)
    for i in range(len(list)):
        if list[i].find('\tdevice') != -1:
            temp = list[i].split('\t')
            devlist.append(temp[0])
    return devlist


connectdevice = input('请输入每次要同时连接的设备数:')
number = int(connectdevice.strip())

while True:
    lists = getdevlist()
    devnum = len(lists)
    id = 1
    tempdevlist = getdevlist()
    if devnum < number:
        print(f'\n设备未全部识别，应识别{number}台设备!\n当前已识别{devnum}台设备,请连接设备并等待识别:\n\n')
        for i in range(devnum):
            print(f'设备{id}: {lists[i]}')
            id = id + 1
    # 等待识别所有设备
    while devnum < number:
        lists = getdevlist()
        curnum = len(lists)
        if curnum > devnum:
            for i in range(len(lists)):
                if lists[i] not in tempdevlist:
                    print(f'设备{id}: {lists[i]}')
                    id = id + 1
                    tempdevlist = getdevlist()
            devnum = curnum

    print(f'\n所有设备已全部识别!当前有连接{len(getdevlist())}台设备.\n\n')

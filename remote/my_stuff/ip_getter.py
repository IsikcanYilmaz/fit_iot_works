#!/usr/bin/env python

import serial 
import argparse
import time
from pprint import pprint

devices = {'sender':None, 'receiver':None, 'routers':[]}
ifaceId = None # We assume this is the same number for all devices
args = None

def interact(): # debug
    import code
    code.InteractiveConsole(locals=globals()).interact()

def flushDevice(dev):
    s = dev["ser"]
    s.write("\n\n\n".encode())
    s.read(s.in_waiting)
    s.reset_input_buffer()
    s.reset_output_buffer()

def resetDevice(dev):
    s = dev["ser"]
    s.write("\n\nreboot\n".encode())
    s.reset_input_buffer()
    s.reset_output_buffer()
    time.sleep(1)

def parseIfconfig(rawStr, dev):
    global ifaceId
    iface = None
    hwaddr = None
    linkLocal = None
    for i in rawStr.replace("  ", " ").split("\n"):
        i = i.lstrip()
        # print(i)
        if ("Iface" in i):
            iface = i.split(" ")[1]
            if (not ifaceId):
                ifaceId = iface
                print(f"INTERFACE ID {ifaceId}")
        if ("HWaddr" in i):
            hwaddr = i.split(" ")[2]
            dev["hwaddr"] = hwaddr
        if ("fe80" in i):
            linkLocal = i.split(" ")[2]
            dev["linkLocal"] = linkLocal
    # print(f"IFACE {iface}")
    # print(f"HWADDR {hwaddr}")
    # print(f"LINK LOCAL {linkLocal}")

def getAddresses(dev): 
    s = dev["ser"]
    s.reset_input_buffer() # flush
    s.write("ifconfig\n".encode())
    time.sleep(0.1)
    outStrRaw = s.read(s.in_waiting).decode()
    parseIfconfig(outStrRaw, dev)

def setGlobalAddress(dev):
    s = dev["ser"]
    s.reset_input_buffer()
    s.write(f"ifconfig {ifaceId} add 2001::{dev['id']}\n".encode())
    time.sleep(0.1)
    resp = s.read(s.in_waiting).decode()
    print(">", resp)

def setRplRoot(dev):
    s = dev["ser"]
    s.reset_input_buffer()
    s.write(f"rpl root {ifaceId} 2001::{dev['id']}\n".encode())
    time.sleep(0.1)
    resp = s.read(s.in_waiting).decode()
    print(">", resp)

def main():
    global args
    parser = argparse.ArgumentParser()
    parser.add_argument("sender")
    parser.add_argument("receiver")
    parser.add_argument("-r", "--router", nargs="*")
    parser.add_argument("--rpl", type=bool, default=True)
    parser.add_argument("--manual_route", type=bool, default=False)
    parser.add_argument("--fitiot", type=bool, default=False)
    args = parser.parse_args()

    print(f"SENDER {args.sender}, RECEIVER {args.receiver}, ROUTER(s) {args.router}")
    print(f"RPL {args.rpl}, FITIOT {args.fitiot}")

    devices["sender"] = {"port":args.sender, "id":1, "ser":serial.Serial(args.sender)}
    devices["receiver"] = {"port":args.receiver, "ser":serial.Serial(args.receiver)}
    flushDevice(devices["sender"])
    flushDevice(devices["receiver"])
    if (args.router):
        for j, i in enumerate(args.router):
            devices["routers"].append({"port":i, "id":j+2, "ser":serial.Serial(i)})
            flushDevice(devices["routers"][-1])
            getAddresses(devices["routers"][-1])
            setGlobalAddress(devices["routers"][-1])
    devices["receiver"]["id"] = len(devices["routers"])+2

    getAddresses(devices["sender"])
    getAddresses(devices["receiver"])

    setGlobalAddress(devices["sender"])
    setGlobalAddress(devices["receiver"])

    setRplRoot(devices["sender"])

    pprint(devices)




if __name__ == "__main__":
    main()

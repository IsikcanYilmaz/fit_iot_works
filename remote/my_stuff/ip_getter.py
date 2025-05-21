#!/usr/bin/env python

import serial 
import argparse
import time
from pprint import pprint

devices = {'sender':None, 'receiver':None, 'routers':[]}
ifaceId = None # We assume this is the same number for all devices

def interact():
    import code
    code.InteractiveConsole(locals=globals()).interact()

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
        if ("HWaddr" in i):
            hwaddr = i.split(" ")[2]
            dev["hwaddr"] = hwaddr
        if ("fe80" in i):
            linkLocal = i.split(" ")[2]
            dev["linkLocal"] = linkLocal
    print(f"IFACE {iface}")
    print(f"HWADDR {hwaddr}")
    print(f"LINK LOCAL {linkLocal}")

def prepSender():
    s = devices["sender"]["ser"]
    s.reset_input_buffer() # flush
    s.write("ifconfig\n".encode())
    time.sleep(0.1)
    outStrRaw = s.read(s.in_waiting).decode()
    parseIfconfig(outStrRaw, devices["sender"])

def prepReceiver():
    pass

def prepRouters():
    pass

def main():
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

    devices["sender"] = {"port":args.sender, "ser":serial.Serial(args.sender)}
    devices["receiver"] = {"port":args.receiver, "ser":serial.Serial(args.receiver)}
    if (args.router):
        for i in args.router:
            devices["routers"].append({"port":i, "ser":serial.Serial(i)})


    prepSender()
    pprint(devices)




if __name__ == "__main__":
    main()

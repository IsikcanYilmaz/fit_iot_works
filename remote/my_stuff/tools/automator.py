#!/usr/bin/env python

import serial 
import argparse
import time
import sys, os
import json
import pdb
from common import *
from pprint import pprint

SERIAL_TIMEOUT_S = 10
EXPERIMENT_TIMEOUT_S = 60

devices = {'sender':None, 'receiver':None, 'routers':[]}
ifaceId = None # We assume this is the same number for all devices
args = None

def parseIfconfig(dev, rawStr):
    global ifaceId
    iface = None
    hwaddr = None
    linkLocalAddr = None
    globalAddr = None
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
            linkLocalAddr = i.split(" ")[2]
            dev["linkLocalAddr"] = linkLocalAddr
        if ("global" in i):
            globalAddr = i.split(" ")[2]
            dev["globalAddr"] = globalAddr
            
def getAddresses(dev): 
    outStrRaw = sendSerialCommand(dev, "ifconfig")
    parseIfconfig(dev, outStrRaw)

def setGlobalAddress(dev):
    outStrRaw = sendSerialCommand(dev, f"ifconfig {ifaceId} add 2001::{dev['id']}")
    print(">", outStrRaw)

def unsetGlobalAddress(dev):
    outStrRaw = sendSerialCommand(dev, f"ifconfig {ifaceId} del {dev['globalAddr']}")
    print(">", outStrRaw)

def unsetRpl(dev):
    outStrRaw = sendSerialCommand(dev, f"rpl rm {ifaceId}")
    print(">", outStrRaw)

def setRplRoot(dev):
    outStrRaw = sendSerialCommand(dev, f"rpl root {ifaceId} 2001::{dev['id']}")
    print(">", outStrRaw)

def pingTest(srcDev, dstDev):
    dstIp = dstDev["globalAddr"]
    print(f"{srcDev['globalAddr']} Pinging {dstIp}")
    outStrRaw = sendSerialCommand(srcDev, f"ping {dstIp}", cooldownS=5)
    print(">", outStrRaw)

def experiment(mode=1, delayus=10000, payloadsizebytes=32, transfersizebytes=4096, resultsDir="./"):
    global devices
    txDev = devices["sender"]
    txSer = txDev["ser"]
    rxDev = devices["receiver"]
    rxSer = rxDev["ser"]
    routers = devices["routers"]

    outFilenamePrefix = f"m{mode}_delay{delayus}_pl{payloadsizebytes}_tx{transfersizebytes}_"

    txOut = ""
    rxOut = ""

    print("----------------")
    print(f"EXPERIMENT mode:{mode} delayus:{delayus} payloadsizebytes:{payloadsizebytes} transfersizebytes:{transfersizebytes}")

    flushDevice(rxDev)
    flushDevice(txDev)
    
    rxOut += sendSerialCommand(rxDev, "iperf receiver")
    txOut += sendSerialCommand(txDev, f"iperf config mode {mode} delayus {delayus} payloadsizebytes {payloadsizebytes} transfersizebytes {transfersizebytes}", cooldownS=2)
    txOut += sendSerialCommand(txDev, "iperf sender")

    print("RX:", rxOut)
    print("TX:", txOut)
    
    # pdb.set_trace()

    now = time.time()
    while("done" not in txOut):
        raw = txSer.readline().decode()
        print(f">{raw}")
        txOut += raw
        if (time.time() - now > EXPERIMENT_TIMEOUT_S):
            print("EXPERIMENT TIMEOUT")
            return

    rxOut += rxSer.read(rxSer.in_waiting).decode()
    rxJsonRaw = sendSerialCommand(rxDev, "iperf results json").replace("[IPERF][I] ", "")
    txJsonRaw = sendSerialCommand(txDev, "iperf results json").replace("[IPERF][I] ", "")

    rxOut += rxJsonRaw
    txOut += txJsonRaw

    rxOut += sendSerialCommand(rxDev, "iperf results reset")

    txF = open(f"{resultsDir}/{outFilenamePrefix}txout.txt", "w")   
    txF.write(txOut)
    txF.close()

    rxF = open(f"{resultsDir}/{outFilenamePrefix}rxout.txt", "w")
    rxF.write(rxOut)
    rxF.close()

    txJsonF = open(f"{resultsDir}/{outFilenamePrefix}tx.json", "w")
    txJsonF.write(parseDirtyJson(txJsonRaw))
    txJsonF.close()

    rxJsonF = open(f"{resultsDir}/{outFilenamePrefix}rx.json", "w")
    rxJsonF.write(parseDirtyJson(rxJsonRaw))
    rxJsonF.close()

    print("----------------")

def bulkExperiments():
    experiment(1, 1000000, 32, 1024, "results")
    time.sleep(1)
    # experiment(1, 750000, 32, 1024, "results")
    # time.sleep(1)
    # experiment(1, 500000, 32, 1024, "results")
    # time.sleep(1)
    # experiment(1, 250000, 32, 1024, "results")
    # time.sleep(1)
    # experiment(1, 100000, 32, 1024, "results")
    # time.sleep(1)
    # experiment(1, 50000, 32, 1024, "results")
    # time.sleep(1)
    # experiment(1, 10000, 32, 1024, "results")

def main():
    global args
    parser = argparse.ArgumentParser()
    parser.add_argument("sender")
    parser.add_argument("receiver")
    parser.add_argument("-r", "--router", nargs="*")
    parser.add_argument("--rpl", action="store_true", default=True)
    parser.add_argument("--experiment", action="store_true", default=False)
    parser.add_argument("--manual_route", type=bool, default=False)
    parser.add_argument("--fitiot", type=bool, default=False)
    args = parser.parse_args()

    print(f"SENDER {args.sender}, RECEIVER {args.receiver}, ROUTER(s) {args.router}")
    print(f"RPL {args.rpl}, FITIOT {args.fitiot}")

    devices["sender"] = {"port":args.sender, "id":1, "ser":serial.Serial(args.sender, timeout=SERIAL_TIMEOUT_S)}
    devices["receiver"] = {"port":args.receiver, "ser":serial.Serial(args.receiver, timeout=SERIAL_TIMEOUT_S)}
    flushDevice(devices["sender"])
    flushDevice(devices["receiver"])

    getAddresses(devices["sender"])
    getAddresses(devices["receiver"])

    unsetRpl(devices["receiver"])
    unsetRpl(devices["sender"])

    if ("globalAddr" in devices["sender"]): # TODO still needs some work: if one of the devices reboots, its easier to reboot every device and have them go thru this script all over again
        unsetGlobalAddress(devices["sender"])

    if ("globalAddr" in devices["receiver"]):
        unsetGlobalAddress(devices["receiver"])

    if (args.router):
        for j, i in enumerate(args.router):
            devices["routers"].append({"port":i, "id":j+2, "ser":serial.Serial(i, timeout=SERIAL_TIMEOUT_S)})
            flushDevice(devices["routers"][-1])
            getAddresses(devices["routers"][-1])
            if ("globalAddr" in devices["routers"][-1]):
                unsetGlobalAddress(devices["receiver"])
            setGlobalAddress(devices["routers"][-1])
            getAddresses(devices["routers"][-1])
    devices["receiver"]["id"] = len(devices["routers"])+2

    setGlobalAddress(devices["sender"])
    getAddresses(devices["sender"])
    setGlobalAddress(devices["receiver"])
    getAddresses(devices["receiver"]) # may not be needed? 

    if (args.rpl):
        setRplRoot(devices["sender"])
        pingTest(devices["sender"], devices["receiver"])
    else:
        pass

    pprint(devices)

    if (args.experiment):
        bulkExperiments()
    

if __name__ == "__main__":
    main()

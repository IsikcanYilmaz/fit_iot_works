#!/usr/bin/env python

import serial 
import argparse
import time
import sys, os
import json
import pdb
from common import *
from pprint import pprint

DEFAULT_RESULTS_DIR = "./results/"
SERIAL_TIMEOUT_S = 10
EXPERIMENT_TIMEOUT_S = 60*10

devices = {'sender':None, 'receiver':None, 'routers':[]}
ifaceId = None # We assume this is the same number for all devices
args = None

"""
Example usage: ./automator.py /dev/ttyACM0 /dev/ttyACM1 --rpl --experiment --results_dir ../results/
"""


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

def unsetRoutes(dev):
    pass

# NOTE AND TODO: This only sets the nib entries for the source and the destination basically. if you want any of the other nodes to be reachable you'll haveto consider the logic for it
def setManualRoutes(devices):
    if (len(devices["routers"]) == 0):
        return
    print("Setting routes manually...")

    # From the sender to the receiver
    print(f"Setting Sender->Receiver {devices['routers'][0]['linkLocalAddr']}")
    outStrRaw = sendSerialCommand(devices["sender"], f"nib route add {ifaceId} {devices['receiver']['globalAddr']} {devices['routers'][0]['linkLocalAddr']}") # Sender routes thru first router towards receiver
    print(">", outStrRaw)

    # From the receiver to the sender
    print(f"Setting Receiver->Sender {devices['routers'][-1]['linkLocalAddr']}")
    outStrRaw = sendSerialCommand(devices["receiver"], f"nib route add {ifaceId} {devices['sender']['globalAddr']} {devices['routers'][-1]['linkLocalAddr']}") # Receiver routes thru last router towards sender
    print(">", outStrRaw)

    for idx, dev in enumerate(devices["routers"]):
        nextHop = ""
        prevHop = ""
        if idx == len(devices["routers"])-1: # Last router in the line. Next hop is the rx
            nextHop = devices["receiver"]["linkLocalAddr"]
            prevHop = devices["routers"][idx-1]["linkLocalAddr"] if len(devices["routers"])>1 else devices["sender"]["linkLocalAddr"]
        elif idx == 0: # First router in the line
            nextHop = devices["routers"][idx+1]["linkLocalAddr"] if len(devices["routers"])>1 else devices["receiver"]["linkLocalAddr"]
            prevHop = devices["sender"]["linkLocalAddr"]
        else: # Router after 0th and before nth
            nextHop = devices["routers"][idx+1]["linkLocalAddr"]
            prevHop = devices["routers"][idx-1]["linkLocalAddr"]

        print(f"Setting R{idx} nextHop:{nextHop} prevHop:{prevHop}")

        # tx->rx
        outStrRaw = sendSerialCommand(dev, f"nib route add {ifaceId} {devices['receiver']['globalAddr']} {nextHop}")
        print(">", outStrRaw)

        # rx->tx
        outStrRaw = sendSerialCommand(dev, f"nib route add {ifaceId} {devices['sender']['globalAddr']} {prevHop}")
        print(">", outStrRaw)

def setIperfTarget(dev, targetGlobalAddr):
    outStrRaw = sendSerialCommand(dev, f"iperf target {targetGlobalAddr}")
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

    outFilenamePrefix = f"m{mode}_delay{delayus}_pl{payloadsizebytes}_tx{transfersizebytes}_routers{len(devices['routers'])}"

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

    # TODO Hacky parsing below. Could do better formatting on the fw side
    rxJsonRaw = sendSerialCommand(rxDev, "iperf results all", captureOutput=True, cooldownS=1).replace("[IPERF][I] ", "").split("\n")[1:-1]
    txJsonRaw = sendSerialCommand(txDev, "iperf results all", captureOutput=True, cooldownS=1).replace("[IPERF][I] ", "").split("\n")[1:-1]

    rxJsonRaw = " ".join(rxJsonRaw)
    txJsonRaw = " ".join(txJsonRaw)

    rxOut += rxJsonRaw
    txOut += txJsonRaw

    rxOut += sendSerialCommand(rxDev, "iperf results reset")

    # txF = open(f"{resultsDir}/{outFilenamePrefix}_txout.txt", "w")   
    # txF.write(txOut)
    # txF.close()
    #
    # rxF = open(f"{resultsDir}/{outFilenamePrefix}_rxout.txt", "w")
    # rxF.write(rxOut)
    # rxF.close()
    #
    # txJsonF = open(f"{resultsDir}/{outFilenamePrefix}_tx.json", "w")
    # txJsonF.write(parseDirtyJson(txJsonRaw))
    # txJsonF.close()
    #
    # rxJsonF = open(f"{resultsDir}/{outFilenamePrefix}_rx.json", "w")
    # rxJsonF.write(parseDirtyJson(rxJsonRaw))
    # rxJsonF.close()

    rxJson = json.loads(rxJsonRaw)
    txJson = json.loads(txJsonRaw)

    overallJson = {"rx":rxJson["results"], "tx":txJson["results"], "config":txJson["config"]}
    with open(f"{resultsDir}/{outFilenamePrefix}.json", "w") as f:
        json.dump(overallJson, f, indent=4)

    # print("RX JSON", rxJson, "\n")
    # print("TX JSON", txJson, "\n")
    pprint(overallJson)

    print(f"Results written to {resultsDir}")
    print("----------------")

def bulkExperiments(resultsDir):
    if (not os.path.isdir(resultsDir)):
        print(f"{resultsDir} not found! Creating")
        try:
            os.mkdir(resultsDir)
        except Exception as e:
            print(e)
            print(f"Exception while creating results dir {resultsDir}. Will use . as resultsDir")
            resultsDir = "./"

    # delayUsArr = [1000000, 750000, 500000, 250000, 100000, 50000, 10000, 5000, 1000, 100]
    # payloadSizeArr = [64, 32, 16, 8]
    # transferSizeArr = [1024]

    delayUsArr = [10000]
    payloadSizeArr = [64]
    transferSizeArr = [4096]
    
    mode = 1

    # Sweep
    for delayUs in delayUsArr:
        for payloadSize in payloadSizeArr:
            for transferSize in transferSizeArr:
                experiment(1, delayUs, payloadSize, transferSize, resultsDir)
                time.sleep(2)

def main():
    global args
    parser = argparse.ArgumentParser()
    parser.add_argument("sender")
    parser.add_argument("receiver")
    parser.add_argument("-r", "--router", nargs="*")
    parser.add_argument("--rpl", action="store_true", default=False)
    parser.add_argument("--experiment", action="store_true", default=False)
    parser.add_argument("--fitiot", type=str, help="The location of the FIT-IOT nodes, e.g. saclay")
    parser.add_argument("--results_dir", type=str)
    args = parser.parse_args()

    # check if fit-iot location is valid
    if args.fitiot is not None and args.fitiot not in ["grenoble", "lille", "paris", "saclay", "strasbourg", "toulouse"]:        
        raise Exception("The FIT-IOT option is selected, but the site name is invalid.")

    print(f"SENDER {args.sender}, RECEIVER {args.receiver}, ROUTER(s) {args.router}")
    print(f"RPL {args.rpl}, FITIOT {args.fitiot}, EXPERIMENT {args.experiment}")

    devices["sender"] = {"port":args.sender, "id":1, "ser":
                         serial.Serial(args.sender, timeout=SERIAL_TIMEOUT_S) if not args.fitiot
                    else RemoteSerial(deviceName=args.sender, siteName=args.fitiot, timeout=SERIAL_TIMEOUT_S)}
    devices["receiver"] = {"port":args.receiver, "ser":
                         serial.Serial(args.receiver, timeout=SERIAL_TIMEOUT_S) if not args.fitiot
                    else RemoteSerial(deviceName=args.receiver, siteName=args.fitiot, timeout=SERIAL_TIMEOUT_S)}
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
            r = {"port":i, "id":j+2, "ser":serial.Serial(i, timeout=SERIAL_TIMEOUT_S)}
            flushDevice(r)
            getAddresses(r)
            unsetRpl(r)
            if ("globalAddr" in r):
                unsetGlobalAddress(r)
            setGlobalAddress(r)
            getAddresses(r)
            devices["routers"].append(r)
    devices["receiver"]["id"] = len(devices["routers"])+2

    setGlobalAddress(devices["sender"])
    getAddresses(devices["sender"])
    setGlobalAddress(devices["receiver"])
    getAddresses(devices["receiver"]) # may not be needed? 

    if (args.rpl):
        setRplRoot(devices["sender"])
    else:
        setManualRoutes(devices)

    if (len(devices["routers"]) > 0):
        setIperfTarget(devices["sender"], devices["receiver"]["globalAddr"])

    # pdb.set_trace()
    devices["sender"]["ser"].stop()
    devices["receiver"]["ser"].stop()

    pingTest(devices["sender"], devices["receiver"])

    pprint(devices)

    if (args.experiment):
        bulkExperiments(resultsDir=(args.results_dir if args.results_dir else DEFAULT_RESULTS_DIR))
    

if __name__ == "__main__":
    main()

#!/usr/bin/env python3

import serial 
import argparse
import time
import sys, os
import json
import pdb
import traceback
import threading
from common import *
from pprint import pprint

DEFAULT_RESULTS_DIR = "./results/"
SERIAL_TIMEOUT_S = 10
EXPERIMENT_TIMEOUT_S = 60*10
MULTITHREADED = True

devices = {'sender':None, 'receiver':None, 'routers':[]}
ifaceId = None # We assume this is the same number for all devices
args = None
comm = None

threads = []

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

def getL2Stats(dev):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, f"ifconfig {ifaceId} stats l2")
    print("<", outStrRaw)

def getIpv6Stats(dev):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, f"ifconfig {ifaceId} stats ipv6")
    print("<", outStrRaw)

def resetNetstats(dev):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, f"ifconfig {ifaceId} stats all reset")
    print("<", outStrRaw)
            
def getAddresses(dev):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, "ifconfig")
    parseIfconfig(dev, outStrRaw)

def setGlobalAddress(dev):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, f"ifconfig {ifaceId} add 2001::{dev['id']}")
    print("<", outStrRaw)

def unsetGlobalAddress(dev):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, f"ifconfig {ifaceId} del {dev['globalAddr']}")
    print("<", outStrRaw)

def unsetRpl(dev):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, f"rpl rm {ifaceId}")
    print("<", outStrRaw)

def setRplRoot(dev):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, f"rpl root {ifaceId} 2001::{dev['id']}")
    print("<", outStrRaw)

def unsetRoutes(dev):
    pass

# NOTE AND TODO: This only sets the nib entries for the source and the destination basically. if you want any of the other nodes to be reachable you'll haveto consider the logic for it
def setManualRoutes(devices):
    global comm
    if (len(devices["routers"]) == 0):
        return
    print("Setting routes manually...")

    # From the sender to the receiver
    print(f"Setting Sender->Receiver {devices['routers'][0]['linkLocalAddr']}")
    outStrRaw = comm.sendSerialCommand(devices["sender"], f"nib route add {ifaceId} {devices['receiver']['globalAddr']} {devices['routers'][0]['linkLocalAddr']}") # Sender routes thru first router towards receiver
    print("<", outStrRaw)

    # From the receiver to the sender
    print(f"Setting Receiver->Sender {devices['routers'][-1]['linkLocalAddr']}")
    outStrRaw = comm.sendSerialCommand(devices["receiver"], f"nib route add {ifaceId} {devices['sender']['globalAddr']} {devices['routers'][-1]['linkLocalAddr']}") # Receiver routes thru last router towards sender
    print("<", outStrRaw)

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
        outStrRaw = comm.sendSerialCommand(dev, f"nib route add {ifaceId} {devices['receiver']['globalAddr']} {nextHop}")
        print("<", outStrRaw)

        # rx->tx
        outStrRaw = comm.sendSerialCommand(dev, f"nib route add {ifaceId} {devices['sender']['globalAddr']} {prevHop}")
        print("<", outStrRaw)

def setIperfTarget(dev, targetGlobalAddr):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, f"iperf target {targetGlobalAddr}")
    print("<", outStrRaw)

def pingTest(srcDev, dstDev):
    global comm
    dstIp = dstDev["globalAddr"]
    print(f"{srcDev['globalAddr']} Pinging {dstIp}")
    outStrRaw = comm.sendSerialCommand(srcDev, f"ping {dstIp}", cooldownS=10, captureOutput=True)
    print("<", outStrRaw)

def setTxPower(dev, txpower):
    global comm
    outStrRaw = comm.sendSerialCommand(dev, f"setpwr {txpower}")
    print("<", outStrRaw)

def setRetrans(dev, retrans):
    global comm, args
    outStrRaw = ""
    if (args.fitiot):
        outStrRaw = comm.sendSerialCommand(dev, f"ifconfig {ifaceId} set csma_retries {retrans}")
        outStrRaw += comm.sendSerialCommand(dev, f"ifconfig {ifaceId} set retrans {retrans}")
    else:
        outStrRaw = comm.sendSerialCommand(dev, f"setretrans {retrans}")
    print("<", outStrRaw)

def setAllDevicesRetrans(retrans):
    global args, devices
    if (args.retrans != None):
        print(f"Setting L2 retransmissions to {args.retrans}")
        setRetrans(devices["sender"], args.retrans)
        setRetrans(devices["receiver"], args.retrans)
        for dev in devices["routers"]:
            setRetrans(dev, args.retrans)

def parseDeviceJsons(j):
    global args
    # Expects {"rx":{}, "tx":{}, "config":{}}
    timeDiffSecs = j["tx"]["timeDiff"] / 1000000
    numLostPackets = j["tx"]["numSentPkts"] - (j["rx"]["numReceivedPkts"] - j["rx"]["numDuplicates"])
    lossPercent = numLostPackets * 100 / j["tx"]["numSentPkts"]
    sendRate = j["tx"]["numSentPkts"] * j["config"]["payloadSizeBytes"] / timeDiffSecs
    receiveRate = (j["rx"]["numReceivedPkts"] - j["rx"]["numDuplicates"]) * j["config"]["payloadSizeBytes"] / timeDiffSecs
    return {"timeDiffSecs":timeDiffSecs, "numLostPackets":numLostPackets, "lossPercent":lossPercent, "sendRate":sendRate, "receiveRate":receiveRate}

def averageRoundsJsons(j):
    avgNumLostPkts = sum([j[i]["results"]["numLostPackets"] for i in range(0, len(j))])/len(j)
    avgLossPercent = sum([j[i]["results"]["lossPercent"] for i in range(0, len(j))])/len(j)
    avgSendRate = sum([j[i]["results"]["sendRate"] for i in range(0, len(j))])/len(j)
    avgReceiveRate = sum([j[i]["results"]["receiveRate"] for i in range(0, len(j))])/len(j)
    return {"avgLostPackets":avgNumLostPkts, "avgLossPercent":avgLossPercent, "avgSendRate":avgSendRate, "avgReceiveRate":avgReceiveRate}

def resetAllDevicesNetstats():
    global devices
    resetNetstats(devices["sender"])
    resetNetstats(devices["receiver"])
    for dev in devices["routers"]:
        resetNetstats(dev)

def experiment(mode=1, delayus=50000, payloadsizebytes=32, transfersizebytes=4096, rounds=1, resultsDir="./"):
    global devices, comm, args
    txDev = devices["sender"]
    rxDev = devices["receiver"]
    routers = devices["routers"]

    outFilenamePrefix = f"m{mode}_delay{delayus}_pl{payloadsizebytes}_tx{transfersizebytes}_routers{len(devices['routers'])}"
    overallJson = []

    averagesFilename = f"{resultsDir}/{outFilenamePrefix}_averages.json"
    experimentFilename = f"{resultsDir}/{outFilenamePrefix}.json"

    for round in range(0, rounds):
        roundFilename = f"{resultsDir}/{outFilenamePrefix}_round{round}.json"

        # The experiment may have been run before and bombed. 
        # Check if the experiment was run before. if so, simply load up that round json
        # if ()

        txOut = ""
        rxOut = ""

        print("----------------")
        print(f"EXPERIMENT mode:{mode} delayus:{delayus} payloadsizebytes:{payloadsizebytes} transfersizebytes:{transfersizebytes} round:{round}")

        comm.flushDevice(rxDev)
        comm.flushDevice(txDev)

        resetAllDevicesNetstats()

        # setAllDevicesRetrans(args.retrans)
        
        rxOut += comm.sendSerialCommand(rxDev, f"iperf config mode {mode} delayus {delayus} payloadsizebytes {payloadsizebytes} transfersizebytes {transfersizebytes}", cooldownS=3)
        txOut += comm.sendSerialCommand(txDev, f"iperf config mode {mode} delayus {delayus} payloadsizebytes {payloadsizebytes} transfersizebytes {transfersizebytes}", cooldownS=3)

        rxOut += comm.sendSerialCommand(rxDev, "iperf receiver")
        txOut += comm.sendSerialCommand(txDev, "iperf sender start")

        print("RX:", rxOut)
        print("TX:", txOut)
        
        now = time.time()

        if (args.fitiot):
            expectedTime = (delayus / 1000000) * (transfersizebytes / payloadsizebytes)
            time.sleep(expectedTime + 10) # TODO better output handling
        else:
            txSer = txDev["ser"]
            rxSer = rxDev["ser"]
            while("done" not in txOut):
                if (txSer.in_waiting > 0):
                    raw = txSer.readline().decode()
                    print(f">{raw}")
                    txOut += raw
                if (time.time() - now > EXPERIMENT_TIMEOUT_S):
                    print("EXPERIMENT TIMEOUT")
                    return
                time.sleep(0.1)
            rxOut += rxSer.read(rxSer.in_waiting).decode()

        # TODO Hacky parsing below. Could do better formatting on the fw side
        parsingSuccess = False
        for i in range(0, 3):
            rxJsonRaw = comm.sendSerialCommand(rxDev, "iperf results all", captureOutput=True, cooldownS=1).replace("[IPERF][I] ", "").split("\n")[1:-1]
            print("<", rxJsonRaw)
            txJsonRaw = comm.sendSerialCommand(txDev, "iperf results all", captureOutput=True, cooldownS=1).replace("[IPERF][I] ", "").split("\n")[1:-1]
            print("<", txJsonRaw)

            rxJsonRaw = " ".join(rxJsonRaw)
            txJsonRaw = " ".join(txJsonRaw)

            try:
                rxJson = json.loads(rxJsonRaw)
                txJson = json.loads(txJsonRaw)
                parsingSuccess = True
                break # If success, break out of the loop. otherwise, try 3 times
            except Exception as e:
                print(traceback.format_exc())
                print("trying again")
                comm.flushDevice(rxDev)
                comm.flushDevice(txDev)
                time.sleep(1)

        if not parsingSuccess:
            print("Couldnt parse json results!")
            return

        rxOut += rxJsonRaw
        txOut += txJsonRaw

        rxOut += comm.sendSerialCommand(rxDev, "iperf results reset")
        txOut += comm.sendSerialCommand(txDev, "iperf results reset")

        rxOut += comm.sendSerialCommand(rxDev, "iperf stop")
        txOut += comm.sendSerialCommand(txDev, "iperf stop")

        deviceJson = {"rx":rxJson["results"], "tx":txJson["results"], "config":txJson["config"]}
        roundOverallJson = {"deviceoutput":deviceJson, "results":parseDeviceJsons(deviceJson)}

        with open(roundFilename, "w") as f:
            json.dump(roundOverallJson, f, indent=4)

        pprint(roundOverallJson)
        overallJson.append(roundOverallJson)

    with open(experimentFilename, "w") as f:
        json.dump(overallJson, f, indent=4)

    overallAveragesJson = averageRoundsJsons(overallJson)
    with open(averagesFilename, "w") as f:
        json.dump(overallAveragesJson, f, indent=4)

    print(f"Results written to {resultsDir}")
    print("----------------")

def bulkExperiments(resultsDir):
    # Create directory if needed
    if (not os.path.isdir(resultsDir)):
        print(f"{resultsDir} not found! Creating")
        try:
            os.mkdir(resultsDir)
        except Exception as e:
            print(e)
            print(f"Exception while creating results dir {resultsDir}. Will use . as resultsDir")
            resultsDir = "./"

    delayUsArr = [5000, 10000, 15000, 20000, 25000, 30000]
    payloadSizeArr = [32, 16, 8]
    transferSizeArr = [4096]
    rounds = 20
    mode = 1

    # Write down the config
    with open(f"{resultsDir}/config.txt", "w") as f:
        f.write(" ".join(sys.argv))
        f.write(f"\ndelayUsArr:{delayUsArr}, payloadSizeArr:{payloadSizeArr}, transferSizeArr:{transferSizeArr}, rounds:{rounds}")

    # Sweep
    experimentCount = 0
    numExperiments = len(delayUsArr) * len(payloadSizeArr) * len(transferSizeArr)
    for delayUs in delayUsArr:
        for payloadSize in payloadSizeArr:
            for transferSize in transferSizeArr:
                print(f"Running experiment {experimentCount}/{numExperiments}")
                experiment(1, delayUs, payloadSize, transferSize, rounds, resultsDir)
                experimentCount += 1
                time.sleep(2)

def tester(dev):
    getL2Stats(dev)
    getIpv6Stats(dev)
    resetNetstats(dev)

def main():
    global args, comm
    parser = argparse.ArgumentParser()
    parser.add_argument("sender")
    parser.add_argument("receiver")
    parser.add_argument("-r", "--router", nargs="*")
    parser.add_argument("--rpl", action="store_true", default=False)
    parser.add_argument("--experiment_test", action="store_true", default=False)
    parser.add_argument("--experiment", action="store_true", default=False)
    parser.add_argument("--fitiot", action="store_true", default=False)
    parser.add_argument("--results_dir", type=str)
    parser.add_argument("--txpower", type=int)
    parser.add_argument("--retrans", type=int)
    parser.add_argument("--test", action="store_true", default=False)
    args = parser.parse_args()

    # print(args)
    # return

    print(f"SENDER {args.sender}, RECEIVER {args.receiver}, ROUTER(s) {args.router}")
    print(f"RPL {args.rpl}, FITIOT {args.fitiot}, EXPERIMENT {args.experiment}")

    comm = DeviceCommunicator(args.fitiot)

    if (args.fitiot): # TODO make this dictionary a class and have this distinction logic be done in its constructor
        devices["sender"] = {"name":args.sender, "id":1}
        devices["receiver"] = {"name":args.receiver}
    else:
        devices["sender"] = {"name":args.sender, "id":1, "ser":serial.Serial(args.sender, timeout=SERIAL_TIMEOUT_S)}
        devices["receiver"] = {"name":args.receiver, "ser":serial.Serial(args.receiver, timeout=SERIAL_TIMEOUT_S)}

    comm.flushDevice(devices["sender"])
    comm.flushDevice(devices["receiver"])

    getAddresses(devices["sender"])
    getAddresses(devices["receiver"])

    if (args.txpower != None):
        print(f"Setting txpowers to {args.txpower}")
        setTxPower(devices["sender"], args.txpower)
        setTxPower(devices["receiver"], args.txpower)
        for dev in devices["routers"]:
            setTxPower(dev, args.txpower)

    unsetRpl(devices["receiver"])
    unsetRpl(devices["sender"])

    if ("globalAddr" in devices["sender"]): # TODO still needs some work: if one of the devices reboots, its easier to reboot every device and have them go thru this script all over again
        unsetGlobalAddress(devices["sender"])

    if ("globalAddr" in devices["receiver"]):
        unsetGlobalAddress(devices["receiver"])

    if (args.router):
        for j, i in enumerate(args.router):
            if (args.fitiot):
                r = {"name":i, "id":j+2}
            else:
                r = {"name":i, "id":j+2, "ser":serial.Serial(i, timeout=SERIAL_TIMEOUT_S)}
            comm.flushDevice(r)
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

    setAllDevicesRetrans(args.retrans)

    if (args.rpl):
        setRplRoot(devices["sender"])
    else:
        setManualRoutes(devices)

    if (len(devices["routers"]) > 0):
        setIperfTarget(devices["sender"], devices["receiver"]["globalAddr"])

    # pdb.set_trace()

    pingTest(devices["sender"], devices["receiver"])

    pprint(devices)

    if (args.test):
        tester(devices["sender"])
        tester(devices["receiver"])
        return

    if (args.experiment_test):
        experiment()
    elif (args.experiment):
        bulkExperiments(resultsDir=(args.results_dir if args.results_dir else DEFAULT_RESULTS_DIR))
    print("DONE")
    

if __name__ == "__main__":
    main()

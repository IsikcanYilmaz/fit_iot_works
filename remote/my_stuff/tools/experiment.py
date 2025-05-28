#!/usr/bin/env python3

import serial 
import argparse
import time
import sys
from common import *
from pprint import pprint

# Expects ip_setter.py to have run



def experiment(devices, mode=1, delayus=10000, payloadsizebytes=32, transfersizebytes=4096):
    txDev = devices["sender"]
    txSer = txDev["ser"]
    rxDev = devices["receiver"]
    rxSer = rxDev["ser"]
    routers = devices["routers"]

    txOut = ""
    rxOut = ""

    print("EXPERIMENT")
    
    rxOut += sendSerialCommand(rxDev, "iperf receiver")
    txOut += sendSerialCommand(txDev, "iperf config mode 1 delayus 10000 payloadsizebytes 32 transfersizebytes 4096", cooldownS=2)
    txOut += sendSerialCommand(txDev, "iperf sender")

    while("done" not in txOut):
        line = txSer.readline().decode()
        print(f">{line}")
        txOut += line

    rxOut += rxSer.read(rxSer.in_waiting).decode()
    rxOut += sendSerialCommand(rxDev, "iperf results json").replace("[IPERF][I] ", "")
    txOut += sendSerialCommand(txDev, "iperf results json").replace("[IPERF][I] ", "")

    txF = open("txout.txt", "w")   
    txF.write(txOut)
    txF.close()

    rxF = open("rxout.txt", "w")
    rxF.write(rxOut)
    rxF.close()

main():
    pass

if __name__ == "__main__":
    main()

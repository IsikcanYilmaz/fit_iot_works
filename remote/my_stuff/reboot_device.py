#!/usr/bin/env python3

import serial
import time
import sys, os
import traceback

TIMEOUT_S = 5

def rebootAll(portList):
    for i in portList:
        if (os.path.islink(i)):
            i = os.readlink(i)
        try:
            print(i)
            ser = serial.Serial(i)
            ser.write("\n\nreboot\n".encode())
            ser.close()
            time.sleep(1)
            timeout = time.time() + TIMEOUT_S
            while((not os.path.exists(i)) and (time.time() < timeout)):
                pass
            if (not os.path.exists(i)):
                print(i, "DID NOT COME BACK UP!")
        except Exception as e:
            print("Error",e)

if __name__ == "__main__":
    print(sys.argv[1:])
    rebootAll(sys.argv[1:])

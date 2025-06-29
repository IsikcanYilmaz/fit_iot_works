#!/usr/bin/env python3

import serial
import time
import subprocess
from pprint import pprint

SERIAL_TIMEOUT_S = 10
SERIAL_COMMAND_BUFFER_SIZE = 50
NC_RETRIES = 3

def interact(): # debug
    import code
    code.InteractiveConsole(locals=globals()).interact()

def sendSerialCommand_local(dev, cmd, cooldownS=1, captureOutput=True):
    s = dev["ser"]
    s.reset_input_buffer()
    s.reset_output_buffer()
    print(f"{dev['name']}>", cmd)
    while(len(cmd) > 0):
        s.write(cmd[0:SERIAL_COMMAND_BUFFER_SIZE].encode())
        cmd = cmd[SERIAL_COMMAND_BUFFER_SIZE:]
        time.sleep(0.05)
    s.write("\n".encode())
    time.sleep(cooldownS)
    if (captureOutput):
        resp = s.read(s.in_waiting).decode()
        return resp

def sendSerialCommand_fitiot(dev, cmd, cooldownS=1, captureOutput=True):
    procCmd = f"echo \'{cmd}\' | nc -q {cooldownS} {dev['name']} 20000"
    out = ""
    trial = 0
    print(f"{dev['name']}>", cmd)
    while (trial <= NC_RETRIES):
        proc = subprocess.Popen(procCmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(cooldownS)
        out = proc.communicate()[0].decode()
        proc.terminate()
        proc.wait()
        if ("refused" in out):
            trial += 1
            print(f"Connection refused. Trying again {trial}/{NC_RETRIES}")
            time.sleep(1)
        else:
            break
    if (trial > NC_RETRIES):
        print("sendSerialCommand_fitiot failed! NC Connection refused")
    if (captureOutput):
        return out

def flushDevice_local(dev):
    s = dev["ser"]
    s.write("\n\n\n".encode())
    s.read(s.in_waiting)
    s.reset_input_buffer()
    s.reset_output_buffer()

def flushDevice_fitiot(dev):
    sendSerialCommand_fitiot(dev, "\n\n", captureOutput=False)

def resetDevice_local(dev):
    s = dev["ser"]
    s.write("\n\nreboot\n".encode())
    s.reset_input_buffer()
    s.reset_output_buffer()
    time.sleep(1)

def resetDevice_fitiot(dev):
    sendSerialCommand_fitiot(dev, "reboot", cooldownS=4, captureOutput=False)

# Hacky. Text from the board isnt cleanly separated. sometimes it comes with extra lines that arent json. this separates them
def parseDirtyJson(text):
    for i in text.split("\n"):
        if "{" in i:
            return i

class Device: # TODO CURRENTLY UNUSED
    def __init__(self):
        pass

class DeviceCommunicator:
    def __init__(self, fitiot=False):
        self.fitiot = fitiot
    
    def sendSerialCommand(self, dev, cmd, cooldownS=3, captureOutput=True):
        if (self.fitiot):
            return sendSerialCommand_fitiot(dev, cmd, cooldownS, captureOutput)
        else:
            return sendSerialCommand_local(dev, cmd, cooldownS, captureOutput)

    def flushDevice(self, dev):
        if (self.fitiot):
            return flushDevice_fitiot(dev)
        else:
            return flushDevice_local(dev)

    def resetDevice(self, dev):
        if (self.fitiot):
            return resetDevice_fitiot(dev)
        else:
            return resetDevice_local(dev)


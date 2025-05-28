#!/usr/bin/env python3

import serial
import time
from pprint import pprint

SERIAL_TIMEOUT_S = 10
SERIAL_COMMAND_BUFFER_SIZE = 50

def interact(): # debug
    import code
    code.InteractiveConsole(locals=globals()).interact()

def sendSerialCommand(dev, cmd, cooldownS=1, captureOutput=True):
    s = dev["ser"]
    s.reset_input_buffer()
    s.reset_output_buffer()
    while(len(cmd) > 0):
        s.write(cmd[0:SERIAL_COMMAND_BUFFER_SIZE].encode())
        cmd = cmd[SERIAL_COMMAND_BUFFER_SIZE:]
        time.sleep(0.05)
    s.write("\n".encode())
    time.sleep(cooldownS)
    if (captureOutput):
        resp = s.read(s.in_waiting).decode()
        return resp

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

# Hacky. Text from the board isnt cleanly separated. sometimes it comes with extra lines that arent json. this separates them
def parseDirtyJson(text):
    for i in text.split("\n"):
        if "{" in i:
            return i


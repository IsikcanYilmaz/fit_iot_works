#!/usr/bin/env python3

import serial
import time
import subprocess
import threading
import signal
from pprint import pprint

SERIAL_TIMEOUT_S = 10
SERIAL_COMMAND_BUFFER_SIZE = 50

class RemoteSerial:
    def __init__(self, deviceName, siteName, timeout):
        self.proc = subprocess.Popen(
            ["ssh", siteName, f"nc {deviceName} 20000"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )

        self.thread = threading.Thread(target=self.readSerial, daemon=True)
        self.thread.start()

    def sendCommand(self, cmd: str, cooldownS=1, captureOutput=True):
        if self.proc.poll() is not None:
            raise RuntimeError("SSH/netcat process is not running (broken pipe)")
    
        if not cmd.endswith("\n"): cmd += "\n"
        self.proc.stdin.write(cmd)
        self.proc.stdin.flush()
        time.sleep(cooldownS)

        if (captureOutput):
            output = []
            while True:
                try:
                    line = self.proc.stdout.readline()
                    if not line: break
                    output.append(line.strip())
                except: break
            return "\n".join(output)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            
    def readSerial(self):
        pass

def interact(): # debug
    import code
    code.InteractiveConsole(locals=globals()).interact()

def sendSerialCommand(dev, cmd: str, cooldownS=1, captureOutput=True):
    s = dev["ser"]

    if isinstance(s, RemoteSerial): # remote device - via ssh and nc
        return s.sendCommand(cmd, cooldownS, captureOutput)

    else: # local device - serial as usual
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
    remote = type(s) == RemoteSerial

    if not remote:
        s.write("\n\n\n".encode())
        s.read(s.in_waiting)
        s.reset_input_buffer()
        s.reset_output_buffer()

def resetDevice(dev):
    s = dev["ser"]
    remote = type(s) == RemoteSerial

    if not remote:
        s.write("\n\nreboot\n".encode())
        s.reset_input_buffer()
        s.reset_output_buffer()
        time.sleep(1)

# Hacky. Text from the board isnt cleanly separated. sometimes it comes with extra lines that arent json. this separates them
def parseDirtyJson(text):
    for i in text.split("\n"):
        if "{" in i:
            return i


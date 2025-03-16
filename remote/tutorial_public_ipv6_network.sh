#!/usr/bin/env bash

# Following https://www.iot-lab.info/learn/tutorials/riot/riot-public-ipv6-m3/

LOGIN="yilmaz"
iotlab-auth -u $LOGIN

RIOT_DIR="$HOME/iot-lab/parts/"
RIOT_PATH="$RIOT_DIR/RIOT/"

source /opt/riot.source

# 1) Start an experiment with 2 M3 nodes called riot_m3
MINUTES="1"
NUM_NODES="2"
DEFAULT_CHANNEL="10"
experimentJson=$(iotlab-experiment submit -n riot_m3 -d $MINUTES -l $NUM_NODES",archi=m3:at86rf231+site=saclay")
experimentRet=$?
experimentId=$(echo $experimentJson | jq '.id') # Remember the experiment identifier returned by this. 

if [ "$experimentRet" != 0 ]; then
	echo "[!] Experiment command returned $experimentRet ! Quitting"
	echo "$experimentJson"
	exit
else
	echo "[*] Experiment id $experimentId"
	echo "$experimentJson"
fi

# 2) Get the code of the 2020.10 release of RIOT from git
if [ ! -d "$RIOT_PATH" ]; then
	echo "[!] RIOT Not found at $RIOT_PATH!"
	mkdir -p $RIOT_PATH
	cd $RIOT_PATH
	git clone https://github.com/RIOT-OS/RIOT.git -b 2020.10-branch
	cd RIOT
fi

# 3) Build the required firmware for the border router node
cd $RIOT_PATH
make ETHOS_BAUDRATE=500000 DEFAULT_CHANNEL=$DEFAULT_CHANNEL BOARD=iotlab-m3 -C examples/gnrc_border_router clean all

# 3a) Use CLI tools to flash the fw
iotlab-node --flash examples/gnrc_border_router/bin/iotlab-m3/gnrc_border_router.elf -l saclay,m3,1

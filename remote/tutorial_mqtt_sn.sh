#!/usr/bin/env bash

# Following https://iot-lab.github.io/learn/tutorials/riot/riot-mqtt-sn-a8-m3/

LOGIN="yilmaz"
# iotlab-auth -u $LOGIN

RIOT_DIR="$HOME/iot-lab/parts/"
RIOT_PATH="$RIOT_DIR/RIOT/"

source /opt/riot.source

echo "MQTT Example"

##############################################################################
# Start an experiment called riot_ipv6_a8 that contains 2 A8 nodes
MINUTES="60"
NUM_NODES="2"
DEFAULT_CHANNEL="10"
SITE="saclay"
kickoffJson=$(iotlab-experiment submit -n riot_a8 -d $MINUTES -l $NUM_NODES",archi=a8:at86rf231+site=$SITE")
kickoffRet=$?
experimentId=$(echo $kickoffJson | jq '.id') # Remember the experiment identifier returned by this. 

if [ "$kickoffRet" != 0 ]; then
	echo "[!] Experiment command returned $kickoffRet ! Quitting"
	echo "$kickoffJson"
	exit
else
	echo "[*] Experiment id $experimentId"
	echo "$kickoffJson"
fi

# Wait a moment until all nodes are up
sleep 2
# iotlab-ssh --verbose wait-for-boot

experimentJson=$(iotlab-experiment get -i $experimentId -p)
nodesJson=$(iotlab-experiment get -i $experimentId -n)

##############################################################################
# Get the code of the 2020.10 release of RIOT from git
mkdir -p ~/A8/riot
cd ~/A8/riot
git clone https://github.com/RIOT-OS/RIOT.git -b 2020.10-branch
cd RIOT

##############################################################################
# Build the required firmware for the border router node. The node node-a8-1 will act as the border router in this experiment. The border firmware is built using the RIOT gnrc_border_router example

# cd $RIOT_PATH
make ETHOS_BAUDRATE=500000 DEFAULT_CHANNEL=$DEFAULT_CHANNEL BOARD=iotlab-m3 -C examples/gnrc_border_router clean all

cp examples/gnrc_border_router/bin/iotlab-a8-m3/gnrc_border_router.elf

##############################################################################
# Build the required firmware for the other node. RIOT gnrc_networking example will be used for this purpose. 

make DEFAULT_CHANNEL=$DEFAULT_CHANNEL BOARD=iotlab-a8-m3 -C examples/gnrc_networking clean all
cp examples/gnrc_networking/bin/iotlab-a8-m3/gnrc_networking.elf ~/A8/

##############################################################################
# Connect to the A8 of the M3 border router: node-a8-1. 
ssh root@node-a8-100

# Then flash the BR firmware on the M3 and build the required RIOT configuration tools: uhcpd (Micro Host Configuration Protocol) and ethos (Ethernet Over Serial).



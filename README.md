# Fit IoT Lab Works
==========================

# Boxes
grenoble.iot-lab.info
saclay.iot-lab.info

# Targets
iotlab-m3

# OSs
- contiki:
    - make TARGET=iotlab-m3

- RIOT:
    - source /opt/riot.source
    - make BOARD=iotlab-m3

# Helpful links
https://www.iot-lab.info/legacy/tutorials/contiki-compilation/index.html

# Intro
git clone https://github.com/iot-lab/iot-lab.git

# Deploying a firmware
Expects an elf binary
Run the following command:
`iotlab-experiment submit -n <name> -d 15 -l 2,archi=m3:at86rf231+site=grenoble+mobile=0,hello-world.elf`
-n takes the name of the experiment
-l number of nodes

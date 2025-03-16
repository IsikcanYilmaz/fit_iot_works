#!/usr/bin/env bash

LOGIN="yilmaz"
EXPERIMENT_NAME="testexperiment"
MINUTES="1"
NUM_NODES="2"

iotlab-auth -u $LOGIN

iotlab-experiment submit -n $EXPERIMENT_NAME -d $MINUTES -l $NUM_NODES",archi=m3:at86rf231+site=saclay

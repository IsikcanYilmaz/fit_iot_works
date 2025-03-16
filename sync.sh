#!/usr/bin/env bash

USER="yilmaz"
REMOTE=( \
	"saclay.iot-lab.info" \
	"grenoble.iot-lab.info" \
	"lille.iot-lab.info" \
)
for i in ${REMOTE[@]}; do
	echo "Syncing $i"
	rsync -a -v remote $USER@$i:~/ &
done
wait $(jobs -p)
echo "Done!"

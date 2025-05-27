#!/usr/bin/env bash

DEVICES=( "/dev/ttyACM0" "/dev/ttyACM1" )
TX="/dev/ttyACM0"
RX="/dev/ttyACM1"

# while [ $# -gt 0 ]; do
#   case "$1" in 
#
#   esac
# done

./ip_getter.py "$TX" "$RX" --rpl

tail -f "$TX" > txout.txt &
tail -f "$RX" > rxout.txt &

sleep 1

echo -ne "\n" > "$TX"
echo -ne "\n" > "$RX"
sleep 0.1

echo -ne "iperf receiver\n" > "$RX"
sleep 0.1

echo -ne "iperf config mode 1 delayus 100000\n" > "$TX"
sleep 0.1
echo -ne "iperf config payloadsizebytes 32 transfersizebytes 4096\n" > "$TX"
sleep 0.1
echo -ne "iperf sender\n" > "$TX"
sleep 0.1

while(true); do
  cat txout.txt | grep "done"
  if [ $? == 0 ]; then
    break
  fi
  sleep 0.1
done

echo -ne "iperf results json\n" > "$TX"
sleep 0.1
echo -ne "iperf results json\n" > "$RX"
sleep 0.5
echo -ne "iperf results reset\n" > "$RX"
sleep 0.5

pkill -P $$


#!/usr/bin/env bash

RIOTBASE="$HOME/KODMOD/CCNL_RIOT/"
BOARD="seeedstudio-xiao-nrf52840"
APPLICATION="ccn-lite-relay"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

# make the project
make RIOTBASE=$RIOTBASE BOARD=$BOARD WERROR=0 
ret="$?"

if [ "$ret" != 0 ]; then
  echo -e "${RED}Make failed!${NC}"
  exit $ret
else
  echo -e "${GREEN}Make success${NC}"
fi

# convert to uf2
$RIOTBASE/dist/tools/uf2/uf2conv.py -f 0xADA52840 bin/"$BOARD"/"$APPLICATION".hex --base 0x1000 -o bin/"$BOARD"/"$APPLICATION".uf2 -c

if [ "$1" ]; then
  PORT="$1"
  echo "$PORT AS DEVICE TO PROGRAM"
fi

# If port info supplied
if [ "$PORT" ]; then
  # copy our uf2 file into the volume
  uname -a | grep -i linux
  if [ $? == 0 ]; then # If linux
    # set our device to boot mode
    stty -F $PORT raw ispeed 1200 ospeed 1200 cs8 -cstopb ignpar eol 255 eof 255
    echo "WAITING FOR VOLUME TO COME UP"
    sleep 3
    cp bin/"$BOARD"/"$APPLICATION".uf2 /media/"$(whoami)"/XIAO-SENSE/
  else # If macos
    # set our device to boot mode
    stty -f $PORT raw ispeed 1200 ospeed 1200 cs8 -cstopb ignpar eol 255 eof 255
    echo "WAITING FOR VOLUME TO COME UP"
    sleep 3
    cp bin/"$BOARD"/"$APPLICATION".uf2 /Volumes/XIAO-SENSE/
  fi

  echo "PROGRAMMED $PORT"
fi

echo "DONE"

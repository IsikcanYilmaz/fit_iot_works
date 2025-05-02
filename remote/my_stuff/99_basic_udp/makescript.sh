#!/usr/bin/env bash

# RIOTBASE="$HOME/KODMOD/CCNL_RIOT/"
RIOTBASE="$HOME/KODMOD/TMP_CCNL_RIOT/"
# RIOTBASE="$HOME/KODMOD/NRF_BROKEN_RIOT/" # 75e22e53e8 broke NRF in RIOT

BOARD="seeedstudio-xiao-nrf52840"
APPLICATION="ccn_nc_demo"

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
  PORT=( "$@" )
  echo "${PORT[@]} AS DEVICE(S) TO PROGRAM"
fi

# If port info supplied flash each supplied port with the compiled product
if [ "$PORT" ]; then
  for p in ${PORT[@]}; do
    echo "$p"

    # copy our uf2 file into the volume
    uname -a | grep -i linux
    if [ $? == 0 ]; then # If linux
      # set our device to boot mode
      stty -F "$p" raw ispeed 1200 ospeed 1200 cs8 -cstopb ignpar eol 255 eof 255
      echo "WAITING FOR VOLUME TO COME UP"
      sleep 3
      cp bin/"$BOARD"/"$APPLICATION".uf2 /media/"$(whoami)"/XIAO-SENSE/
    else # If macos
      # set our device to boot mode
      stty -f "$p" raw ispeed 1200 ospeed 1200 cs8 -cstopb ignpar eol 255 eof 255
      echo "WAITING FOR VOLUME TO COME UP"
      sleep 3
      cp bin/"$BOARD"/"$APPLICATION".uf2 /Volumes/XIAO-SENSE/
    fi

    echo -e "${GREEN}PROGRAMMED $p${NC}"
  done
fi

echo "DONE"

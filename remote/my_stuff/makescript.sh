#!/usr/bin/env bash

RIOTBASE="../../../RIOT/"

BOARD="seeedstudio-xiao-nrf52840"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

# Parse args
while [ $# -gt 0 ]; do
  case "$1" in
    "--board") # Select board. available options: "seeedstudio-xiao-nrf52840" "iotlab-m3" "nrf52850dk"
      shift
      BOARD="$1"
      shift
      ;;
    *)
      PORT+=("$1") 
      shift
      ;;
  esac
done


echo "${GREEN}Building for board $BOARD ${NC}"

# make the project
make RIOTBASE=$RIOTBASE BOARD=$BOARD WERROR=0 
ret="$?"

if [ "$ret" != 0 ]; then
  echo -e "${RED}Make failed!${NC}"
  exit $ret
else
  echo -e "${GREEN}Make success${NC}"
fi

# Find necessary filenames
hexfilename=$(find bin/$BOARD -name "*\.hex")
uf2filename=$(echo "$hexfilename" | sed 's/hex/uf2/g')

# convert to uf2
# First check if our RIOT has the tools installed
# If not, get em
if [ ! -f "$RIOTBASE"/dist/tools/uf2/uf2conv.py ]; then
  echo "uf2conv.py not found. Acquiring it..."
  cd $RIOTBASE/dist/tools/uf2/
  git clone https://github.com/microsoft/uf2.git 
  cp uf2/utils/uf2conv.py .
  cp uf2/utils/uf2families.json .
  chmod a+x uf2conv.py
  cd -
fi

$RIOTBASE/dist/tools/uf2/uf2conv.py -f 0xADA52840 "$hexfilename" --base 0x1000 -o "$uf2filename" -c

# If port info supplied flash each supplied port with the compiled product
if [ "$PORT" ]; then
  echo "${PORT[@]} AS DEVICE(S) TO PROGRAM"
  for p in ${PORT[@]}; do
    echo "$p"

    # copy our uf2 file into the volume
    uname -a | grep -i linux
    if [ $? == 0 ]; then # If linux
      # set our device to boot mode
      stty -F "$p" raw ispeed 1200 ospeed 1200 cs8 -cstopb ignpar eol 255 eof 255
      echo "WAITING FOR VOLUME TO COME UP"
      sleep 3
      cp "$uf2filename" /media/"$(whoami)"/XIAO-SENSE/
    else # If macos
      # set our device to boot mode
      stty -f "$p" raw ispeed 1200 ospeed 1200 cs8 -cstopb ignpar eol 255 eof 255
      echo "WAITING FOR VOLUME TO COME UP"
      sleep 3
      cp "$uf2filename" /Volumes/XIAO-SENSE/
    fi

    echo -e "${GREEN}PROGRAMMED $p${NC}"
  done
fi

echo "DONE"

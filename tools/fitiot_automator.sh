#!/usr/bin/env bash

# Below functions should go into your bashrc
#
function getActiveExperiment()
{
  iotlab-experiment get -e | jq '.Running' | grep -v "\]\|\[" | xargs
}

function getSite()
{
  uname -a | awk '{print $2}'
}

function getNodes()
{
  site=$(getSite)
  iotlab-experiment get -ni | jq ".items[0].$site"
}

function getEveryNode()
{
  nodesRaw=$(getNodes | sed 's/{//g' | sed 's/}//g' | xargs)
  dev=$(echo $nodesRaw | awk '{print $1}' | sed 's/://g')
  nodes=$(echo $nodesRaw | awk '{print $2}' | sed 's/+/ /g')
  nodesArr=( )
  for range in $(echo $nodes); do
    sequence=$(seq $(echo $range | sed 's/-/ /g'))
    for s in ${sequence[@]}; do
      nodesArr="$nodesArr $dev-$s"
    done
  done
  echo ${nodesArr[@]}
}

function getEveryNodeExcept()
{
  everyNode="$(getEveryNode)"
  while [ $# -gt 0 ]; do
    everyNode="$(echo $everyNode | sed "s/$1//g")"
    shift
  done
  echo ${everyNode[@]}
}

# Parse args
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then # Check if we're being sourced or not
  while [ $# -gt 0 ]; do
    case "$1" in
      "--nodes") # Select board. available options: "seeedstudio-xiao-nrf52840" "iotlab-m3" "nrf52850dk"
        shift
        BOARD="$1"
        shift
        ;;
      "--fitiot") # We're on fit iot boards. flash them and such
        shift
        FITIOT=true
        ;;
      *)
        PORT+=("$1") 
        shift
        ;;
    esac
  done
fi

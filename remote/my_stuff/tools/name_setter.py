#!/usr/bin/env python3

import argparse
import iotlabaggregator.common
from iotlabaggregator.serial import SerialAggregator

"""
"""

def read_line(identifier, line):
    print(f"{identifier}: {line}")

def main():
    parser = argparse.ArgumentParser()
    iotlabaggregator.common.add_nodes_selection_parser(parser)
    opts = parser.parse_args()
    opts.with_a8 = False
    print(opts)
    nodes = SerialAggregator.select_nodes(opts)
    nodesStr = ""
    for i in nodes:
        nodesStr += i.replace("m3-", "") + " "
    print(nodesStr)
    with SerialAggregator(nodes, line_handler=read_line) as aggregator:
        for n in nodes:
            name = n.replace("m3-", "")
            aggregator.send_nodes([n], f'setname {name}\n\n')
            aggregator.send_nodes([n], f'sethosts {nodesStr}\n\n')

if __name__ == "__main__":
    main()

- To show the experiments you're running
`iotlab-experiment -u yilmaz -p <password> get -e`

- To show the nodes you own
`iotlab-experiment get -i <exp ID> -p | jq '.nodes'`

- To get a shell into your mcus, lets say you got iotlab-m3s, you do
`nc m3-1.saclay.iot-lab.info 20000` replacing the m3-1 with whichever m3 you got

- To get a shell into your A8 embedded linux machines, you do
`ssh root@node-a8-78` where 78 is the node ID. you need to be in the same site's ssh frontend

- To flash firmware:
`iotlab-node -fl <elf file path>`
I think this flashes everything you own. you can specify which nodes to not flash. neat

- Links of interest
https://github.com/iot-lab/cli-tools 

# gnrc_networking example

# ccn-lite-relay example
Example setup:
- ccnl_cs /riot/jon/asd ASDQWE # On first node
- ccnl_fib add /riot/jon/asd ff:ff:ff:ff:ff:ff # On second node
- ccnl_int /riot/jon/asd # On second node

# xor timings
# on iotlab M3 board
xor de ^ ad = 73 in 1 usecs
xor dead ^ beef = 6042 in 1 usecs
xor deadbeef ^ beefdead = 60426042 in 1 usecs
xor 128 bytes in 33 usecs
xor 256 bytes in 65 usecs
xor 512 bytes in 129 usecs
xor 1024 bytes in 257 usecs
xor 8192 bytes in 2049 usecs
xor 16384 bytes in 4097 usecs

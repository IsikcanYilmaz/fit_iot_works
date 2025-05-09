USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_ipv6_router_default 
USEMODULE += gnrc_icmpv6 #
USEMODULE += gnrc_icmpv6_echo # 
USEMODULE += gnrc_ndp # Neighbor Discovery Protocol
USEMODULE += gnrc_rpl
USEMODULE += auto_init_gnrc_rpl
USEMODULE += shell_cmd_gnrc_udp

~ HARDCODED FORWARDING RECIPE ~

- Have above modules
- Assign IPv6 Addresses:
    ifconfig <iface> add 2001::1/64 # change to ::2 and ::3 etc. for the other routers
- On our tx node
    nib route add 7 2001::2/64 <initial ip of our 2. node>  
    nib route add 7 2001::3/64 <initial ip of our 2. node> # These will have our node tx node send data to node 3 thru node 2
- On our middle relay node (2)
    nib route add 7 2001::3/64 <initial ip of our 3. rx node> 
- Start udp server on all.
    udp server start 1
- On our tx node send udp pkts 
    udp send 2001::3 1 asd

~ RPL VERSION TODO ~

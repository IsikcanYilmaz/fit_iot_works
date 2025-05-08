USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_ipv6_router_default 
USEMODULE += gnrc_icmpv6 #
USEMODULE += gnrc_icmpv6_echo # 
USEMODULE += gnrc_ndp # Neighbor Discovery Protocol
USEMODULE += gnrc_rpl
USEMODULE += auto_init_gnrc_rpl

- Have the above modules
- Assign IPv6 Addresses:
    ifconfig <iface> add 2001:db8::1/64 # change to ::2 and ::3 etc. for the other routers
- Designate one node as RPL root
    [On the root node]: rpl root <iface> 2001:db8::1

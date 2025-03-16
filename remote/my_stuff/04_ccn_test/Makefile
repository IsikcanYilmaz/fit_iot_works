##

APPLICAITON ?= ndn_test

BOARD ?= iotlab-m3

BOARD_WHITELIST := fox iotlab-m3

RIOTBASE ?= $${HOME}/RIOT/

DEVELHELP ?= 1

CFLAGS += -DUSE_LINKLAYER
CFLAGS += -DUSE_RONR
CFLAGS += -DCCNL_UAPI_H_
CFLAGS += -DUSE_SUITE_NDNTLV
CFLAGS += -DNEEDS_PREFIX_MATCHING
CFLAGS += -DNEEDS_PACKET_CRAFTING
# CFLAGS += -DUSE_LOGGING

QUIET ?= 0

USEMODULE += ps

USEMODULE += shell
# USEMODULE += shell_cmds_default
USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif

USEMODULE += gnrc_pktdump
USEMODULE += prng_xorshift

USEMODULE += xtimer

USEPKG += ccn-lite

include $(RIOTBASE)/Makefile.include

APPLICATION = ccn-lite-relay

# If no BOARD is found in the environment, use this default:
BOARD ?= native

BOARD_WHITELIST := iotlab-m3 msba2 mulle native native64 nrf52dk nrf52840dk \
                   pba-d-01-kw2x samr21-xpro seeedstudio-xiao-nrf52840


# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../..

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

CFLAGS += -DCCNL_UAPI_H_
CFLAGS += -DNEEDS_PREFIX_MATCHING
CFLAGS += -DNEEDS_PACKET_CRAFTING
CFLAGS += -DUSE_RONR
CFLAGS += -DUSE_SUITE_NDNTLV
CFLAGS += -DUSE_LINKLAYER
CFLAGS += -DUSE_LOGGING
CFLAGS += -DUSE_DEBUG

# DEMO FLAGS
CFLAGS += -DJON_RSSI_LIMITING=-60

# WS281X CONFIG
PIN ?= GPIO_PIN(0, 29)
N ?= 16
TIMER ?= 2
FREQ ?= 16000000

ifneq (, $(PIN))
  CFLAGS += '-DWS281X_PARAM_PIN=$(PIN)'
endif
ifneq (, $(N))
  CFLAGS += '-DWS281X_PARAM_NUMOF=$(N)'
endif
ifneq (, $(TIMER))
  CFLAGS += '-DWS281X_TIMER_DEV=TIMER_DEV($(TIMER))' '-DWS281X_TIMER_MAX_VALUE=TIMER_$(TIMER)_MAX_VALUE'
endif
ifneq (, $(FREQ))
  CFLAGS += '-DWS281X_TIMER_FREQ=$(FREQ)'
endif

###########

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

#neopixels
USEMODULE += ws281x

USEMODULE += ps
USEMODULE += shell_cmds_default
# Include packages that pull up and auto-init the link layer.
# NOTE: 6LoWPAN will be included if IEEE802.15.4 devices are present
USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif
# This application dumps received packets to STDIO using the pktdump module
USEMODULE += gnrc_pktdump
USEMODULE += prng_xorshift

USEMODULE += periph_gpio_irq


EXTERNAL_MODULE_DIRS += $(CURDIR)/../submodules/
USEMODULE += common

CFLAGS += -I$(EXTERNAL_MODULE_DIRS)/common/

USEPKG += ccn-lite

include $(RIOTBASE)/Makefile.include

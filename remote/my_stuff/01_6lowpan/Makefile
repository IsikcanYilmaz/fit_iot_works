APPLICATION = 6lp 

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../..

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

# CFLAGS += -DLOG_LEVEL=LOG_NONE  # disable log output
# CFLAGS += -DCONFIG_GNRC_NETIF_IPV6_ADDRS_NUMOF=2
# CFLAGS += -DGNRC_NETIF_IPV6_GROUPS_NUMOF=2 
# CFLAGS += -DCONFIG_GNRC_IPV6_NIB_NUMOF=1
# CFLAGS += -DCONFIG_GNRC_IPV6_NIB_OFFL_NUMOF=1 # be able to configure at least one route

# DEMO FLAGS
# CFLAGS += -DJON_RSSI_LIMITING=-100
# CFLAGS += -DJON_DEFAULT_TX_POWER=-20
CFLAGS += -DLED0_TO_SHOW_RX
CFLAGS += -DLED1_TO_SHOW_TX
CFLAGS += -DLED2_TO_SHOW_GOOD_RX

CFLAGS += -DNEOPIXEL_SHELL_COMMANDS
CFLAGS += -DTHROTTLERS_SHELL_COMMANDS

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

# Set GNRC_PKTBUF_SIZE via CFLAGS if not being set via Kconfig.
ifndef CONFIG_GNRC_PKTBUF_SIZE
  CFLAGS += -DCONFIG_GNRC_PKTBUF_SIZE=512
endif

###########

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

#neopixels
# USEMODULE += color
# USEMODULE += ws281x

USEMODULE += ps
USEMODULE += shell_cmds_default
# Include packages that pull up and auto-init the link layer.
# NOTE: 6LoWPAN will be included if IEEE802.15.4 devices are present
# USEMODULE += netdev_default
# USEMODULE += auto_init_gnrc_netif
# USEMODULE += gnrc_netif_single
# Specify the minimum networking modules for IPv6
# USEMODULE += gnrc_ipv6
# Additional networking modules that can be dropped if not needed
# USEMODULE += gnrc_icmpv6_echo
# Use minimal standard PRNG
# USEMODULE += prng_minstd
# This application dumps received packets to STDIO using the pktdump module
# USEMODULE += gnrc_pktdump
# USEMODULE += prng_xorshift

# USEMODULE += periph_gpio_irq

# USEMODULE += benchmark_udp
# USEMODULE += shell_cmd_gnrc_udp

USEMODULE += ztimer

EXTERNAL_MODULE_DIRS += $(CURDIR)/../submodules/
USEMODULE += button
USEMODULE += neopixel
USEMODULE += onboard_leds
USEMODULE += throttler
USEMODULE += udp_stress
# USEMODULE += udp_multihop


# CFLAGS += -I$(EXTERNAL_MODULE_DIRS)/common/
CFLAGS += -I$(EXTERNAL_MODULE_DIRS)/button/
CFLAGS += -I$(EXTERNAL_MODULE_DIRS)/neopixel/
CFLAGS += -I$(EXTERNAL_MODULE_DIRS)/onboard_leds/
CFLAGS += -I$(EXTERNAL_MODULE_DIRS)/throttler/

# USEPKG += ccn-lite

include $(RIOTBASE)/Makefile.include

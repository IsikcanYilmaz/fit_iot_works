# name of your application
APPLICATION = tcp

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../../RIOT

# Set this to 1 to enable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 0

USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_pktdump
USEMODULE += gnrc_txtsnd
USEMODULE += gnrc_netif_single
USEMODULE += gnrc_ipv6
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_icmpv6_echo
USEMODULE += gnrc_sock_tcp
USEMODULE += shell
USEMODULE += shell_cmds_default
USEMODULE += ps

CFLAGS += -DLOG_LEVEL=LOG_NONE \
          -Wall
CFLAGS += -DCONFIG_GNRC_NETIF_IPV6_ADDRS_NUMOF=4 \
          -DGNRC_NETIF_IPV6_GROUPS_NUMOF=2 \
          -DCONFIG_GNRC_IPV6_NIB_NUMOF=1 \
          -DCONFIG_GNRC_IPV6_NIB_OFFL_NUMOF=1 # be able to configure at least one route

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

EXTERNAL_BOARD_DIRS += $(RIOTBASE)/tests/build_system/external_board_dirs/esp-ci-boards

include $(RIOTBASE)/Makefile.include

# Set GNRC_PKTBUF_SIZE via CFLAGS if not being set via Kconfig.
ifndef CONFIG_GNRC_PKTBUF_SIZE
  CFLAGS += -DCONFIG_GNRC_PKTBUF_SIZE=512
endif

# Set a custom channel if needed
# include $(RIOTMAKE)/default-radio-settings.inc.mk

# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I $(srctree)/include/linux/
ccflags-y += -DDYNAMIC_DEBUG_MODULE
# test for stmmac
stmmac-tests-$(CONFIG_AW_STMMAC_SELFTESTS):= stmmac_selftests.o

obj-$(CONFIG_AW_STMMAC_ETH) += sunxi-stmmac.o
sunxi-stmmac-objs:= stmmac_main.o stmmac_ethtool.o stmmac_mdio.o ring_mode.o \
	      chain_mode.o dwmac_lib.o dwmac1000_core.o dwmac1000_dma.o	\
	      dwmac100_core.o dwmac100_dma.o enh_desc.o norm_desc.o	\
	      mmc_core.o stmmac_hwtstamp.o stmmac_ptp.o dwmac4_descs.o	\
	      dwmac4_dma.o dwmac4_lib.o dwmac4_core.o dwmac5.o hwif.o \
	      stmmac_tc.o dwxgmac2_core.o dwxgmac2_dma.o dwxgmac2_descs.o \
	      stmmac_xdp.o stmmac_platform.o dwmac-sunxi.o pcs-xpcs.o pcs-xpcs-nxp.o $(stmmac-tests-y) \

#stmmac-pci-objs:= stmmac_pci.o

obj-$(CONFIG_AW_TSN) += tsn.o
tsn-objs :=tsn_core.o tsn_configfs.o tsn_net.o tsn_header.o

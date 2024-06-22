# SPDX-License-Identifier: GPL-2.0
ccflag-y += -DDYNAMIC_DEBUG_MODULE

ccflags-y += -I $(srctree)/drivers/pci/

pcie_sunxi_host-objs := pcie-sunxi-rc.o pcie-sunxi-dma.o pcie-sunxi-plat.o
obj-$(CONFIG_AW_PCIE_RC) += pcie_sunxi_host.o
obj-$(CONFIG_AW_PCIE_EP) += pcie-sunxi-ep.o

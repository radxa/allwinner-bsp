/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
* Allwinner DWMAC driver.
*
* Copyright(c) 2022-2027 Allwinnertech Co., Ltd.
*
* This file is licensed under the terms of the GNU General Public
* License version 2.  This program is licensed "as is" without any
* warranty of any kind, whether express or implied.
*/

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mdio-mux.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <sunxi-sid.h>
#include <sunxi-stmmac.h>

#include "stmmac.h"
#include "stmmac_platform.h"

#define DWMAC_MODULE_VERSION		"0.0.7"

#define MAC_ADDR_LEN			18
#define SUNXI_DWMAC_MAC_ADDRESS		"00:00:00:00:00:00"

static char mac_str[MAC_ADDR_LEN] = SUNXI_DWMAC_MAC_ADDRESS;
module_param_string(mac_str, mac_str, MAC_ADDR_LEN, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mac_str, "MAC Address String.(xx:xx:xx:xx:xx:xx)");

struct dwmac_variant {
	u32 default_syscon_value;
	bool support_mii;
	bool support_rmii;
	bool support_rgmii;
	u8 rx_delay_max;
	u8 tx_delay_max;
};

struct mii_reg_dump {
	u32 addr;
	u16 reg;
	u16 value;
};

struct sunxi_dwmac {
	const struct dwmac_variant *variant;
	struct mii_reg_dump mii_reg;
	struct clk *phy25m_clk;
	struct device *dev;
	void __iomem *syscfg_base;
	struct regulator *dwmac3v3_supply;

	/* adjust transmit clock delay, value: 0~7 */
	/* adjust receive clock delay, value: 0~31 */
	u32 tx_delay;
	u32 rx_delay;

	bool soc_phy25m_en;
	int interface;
};

static const struct dwmac_variant dwmac200_variant = {
	.default_syscon_value = 0,
	.support_mii = false,
	.support_rmii = true,
	.support_rgmii = true,
	.rx_delay_max = 31,
	.tx_delay_max = 7,
};

/* 1: enable RMII (overrides EPIT) */
#define DWMAC_SYSCON_RMII_EN		BIT(13)
/* Generic system control EMAC_CLK bits */
#define DWMAC_SYSCON_ETXDC_SHIFT	10
#define DWMAC_SYSCON_ERXDC_SHIFT	5
#define DWMAC_SYSCON_ETXDC_MASK		GENMASK(12, 10)
#define DWMAC_SYSCON_ERXDC_MASK		GENMASK(9, 5)
/* EMAC PHY Interface Type */
#define DWMAC_SYSCON_EPIT		BIT(2) /* 1: RGMII, 0: MII */
#define DWMAC_SYSCON_ETCS_MASK		GENMASK(1, 0)
#define DWMAC_SYSCON_ETCS_MII		0x0
#define DWMAC_SYSCON_ETCS_EXT_GMII	0x1
#define DWMAC_SYSCON_ETCS_INT_GMII	0x2

#ifdef MODULE
extern int get_custom_mac_address(int fmt, char *name, char *addr);
#endif

static int sunxi_dwmac_power_on(struct sunxi_dwmac *chip)
{
	int ret;

	if (IS_ERR_OR_NULL(chip->dwmac3v3_supply))
		return 0;

	/* set dwmac pin bank voltage to 3.3v */
	ret = regulator_set_voltage(chip->dwmac3v3_supply, 3300000, 3300000);
	if (ret) {
		dev_err(chip->dev, "Error: set dwmac3v3-supply failed\n");
		return -EINVAL;
	}

	ret = regulator_enable(chip->dwmac3v3_supply);
	if (ret) {
		dev_err(chip->dev, "Error: enable dwmac3v3-supply failed\n");
		return -EINVAL;
	}

	return 0;
}

static void sunxi_dwmac_power_off(struct sunxi_dwmac *chip)
{
	if (IS_ERR_OR_NULL(chip->dwmac3v3_supply))
		return;

	regulator_disable(chip->dwmac3v3_supply);
}

static int sunxi_dwmac_set_syscon(struct sunxi_dwmac *chip)
{
	u32 reg, val;

	reg = chip->variant->default_syscon_value;
	val = chip->tx_delay;
	if (val <= chip->variant->tx_delay_max) {
			reg &= ~(chip->variant->tx_delay_max <<
				 DWMAC_SYSCON_ETXDC_SHIFT);
			reg |= (val << DWMAC_SYSCON_ETXDC_SHIFT);
		} else {
			dev_err(chip->dev, "Error: Invalid TX clock delay: %d\n",
				val);
			return -EINVAL;
	}

	val = chip->rx_delay;
	if (val <= chip->variant->rx_delay_max) {
		reg &= ~(chip->variant->rx_delay_max <<
			 DWMAC_SYSCON_ERXDC_SHIFT);
		reg |= (val << DWMAC_SYSCON_ERXDC_SHIFT);
	} else {
		dev_err(chip->dev, "Error: Invalid RX clock delay: %d\n",
			val);
		return -EINVAL;
	}

	/* Clear interface mode bits */
	reg &= ~(DWMAC_SYSCON_ETCS_MASK | DWMAC_SYSCON_EPIT);
	if (chip->variant->support_rmii)
		reg &= ~DWMAC_SYSCON_RMII_EN;

	switch (chip->interface) {
	case PHY_INTERFACE_MODE_MII:
		/* default */
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		reg |= DWMAC_SYSCON_EPIT | DWMAC_SYSCON_ETCS_INT_GMII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		reg |= DWMAC_SYSCON_RMII_EN | DWMAC_SYSCON_ETCS_EXT_GMII;
		break;
	default:
		dev_err(chip->dev, "Error: Unsupported interface mode: %s",
			phy_modes(chip->interface));
		return -EINVAL;
	}

	writel(reg, chip->syscfg_base);

	return 0;
}

static void sunxi_dwmac_unset_syscon(struct sunxi_dwmac *chip)
{
	writel(chip->variant->default_syscon_value, chip->syscfg_base);
}

static int sunxi_dwmac_init(struct platform_device *pdev, void *priv)
{
	struct sunxi_dwmac *chip = priv;
	int ret;

	ret = sunxi_dwmac_power_on(chip);
	if (ret) {
		dev_err(&pdev->dev, "Error: power on dwmac pin failed\n");
		return ret;
	}

	if (chip->soc_phy25m_en)
		ret = clk_prepare_enable(chip->phy25m_clk);

	if (ret) {
		dev_err(&pdev->dev, "Error: enable phy25m clk failed\n");
		goto err_clk;
	}

	ret = sunxi_dwmac_set_syscon(chip);
	if (ret) {
		dev_err(&pdev->dev, "Error: set syscon failed\n");
		goto err_syscon;
	}

	return 0;

err_syscon:
	if (chip->soc_phy25m_en)
		clk_disable_unprepare(chip->phy25m_clk);
err_clk:
	sunxi_dwmac_power_off(chip);

	return ret;
}

static void sunxi_dwmac_exit(struct platform_device *pdev, void *priv)
{
	struct sunxi_dwmac *chip = priv;

	sunxi_dwmac_power_off(chip);

	if (chip->soc_phy25m_en)
		clk_disable_unprepare(chip->phy25m_clk);

	sunxi_dwmac_unset_syscon(chip);
}

static void sunxi_dwmac_parse_delay_maps(struct sunxi_dwmac *chip)
{
	struct platform_device *pdev = to_platform_device(chip->dev);
	struct device_node *np = pdev->dev.of_node;
	int ret, maps_cnt, i;
	const u8 array_size = 3;
	u32 *maps;
	u16 soc_ver;

	maps_cnt = of_property_count_elems_of_size(np, "delay-maps", sizeof(u32));
	if (maps_cnt <= 0) {
		dev_info(&pdev->dev, "Info: not found delay-maps in dts\n");
		return;
	}

	maps = devm_kcalloc(&pdev->dev, maps_cnt, sizeof(u32), GFP_KERNEL);
	if (!maps)
		return;

	ret = of_property_read_u32_array(np, "delay-maps", maps, maps_cnt);
	if (ret) {
		dev_err(&pdev->dev, "Error: failed to parse delay-maps\n");
		goto err_parse_maps;
	}

	soc_ver = (u16)sunxi_get_soc_ver();
	for (i = 0; i < (maps_cnt / array_size); i++) {
		if (soc_ver == maps[i * array_size]) {
			chip->rx_delay = maps[i * array_size + 1];
			chip->tx_delay = maps[i * array_size + 2];
			dev_info(&pdev->dev, "Info: delay-maps overwrite delay parameters, rx-delay:%d, tx-delay:%d\n",
					chip->rx_delay, chip->tx_delay);
		}
	}

err_parse_maps:
	devm_kfree(&pdev->dev, maps);
}

static int sunxi_dwmac_resource_get(struct platform_device *pdev, struct sunxi_dwmac *chip)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(dev, "Error: Get phy memory failed\n");
		return -ENODEV;
	}

	chip->syscfg_base = devm_ioremap_resource(dev, res);
	if (!chip->syscfg_base) {
		dev_err(dev, "Error: Phy memory mapping failed\n");
		return -ENOMEM;
	}

	chip->soc_phy25m_en = of_property_read_bool(np, "aw,soc-phy25m");
	if (chip->soc_phy25m_en) {
		chip->phy25m_clk = devm_clk_get(dev, "phy25m");
		if (IS_ERR(chip->phy25m_clk)) {
			dev_err(dev, "Error: Get phy25m clk failed\n");
			return -EINVAL;
		}
		dev_info(dev, "Info: Phy use soc25m\n");
	} else
		dev_info(dev, "Info: Phy use osc25m\n");

	chip->dwmac3v3_supply = devm_regulator_get_optional(&pdev->dev, "dwmac3v3");
	if (IS_ERR(chip->dwmac3v3_supply))
		dev_warn(dev, "Warning: dwmac3v3-supply not found\n");

	ret = of_property_read_u32(np, "tx-delay", &chip->tx_delay);
	if (ret) {
		dev_warn(dev, "Warning: Get gmac tx-delay failed, use default 0\n");
		chip->tx_delay = 0;
	}

	ret = of_property_read_u32(np, "rx-delay", &chip->rx_delay);
	if (ret) {
		dev_warn(dev, "Warning: Get gmac rx-delay failed, use default 0\n");
		chip->rx_delay = 0;
	}

	sunxi_dwmac_parse_delay_maps(chip);

	return 0;
}

/**
 * sunxi_parse_read_str - parse the input string for write attri.
 * @str: string to be parsed, eg: "0x00 0x01".
 * @addr: store the phy addr. eg: 0x00.
 * @reg: store the reg addr. eg: 0x01.
 *
 * return 0 if success, otherwise failed.
 */
static int sunxi_parse_read_str(char *str, u16 *addr, u16 *reg)
{
	char *ptr = str;
	char *tstr = NULL;
	int ret;

	/**
	 * Skip the leading whitespace, find the true split symbol.
	 * And it must be 'address value'.
	 */
	tstr = strim(str);
	ptr = strchr(tstr, ' ');
	if (!ptr)
		return -EINVAL;

	/**
	 * Replaced split symbol with a %NUL-terminator temporary.
	 * Will be fixed at end.
	 */
	*ptr = '\0';
	ret = kstrtos16(tstr, 16, addr);
	if (ret)
		goto out;

	ret = kstrtos16(skip_spaces(ptr + 1), 16, reg);

out:
	return ret;
}

/**
 * sunxi_parse_write_str - parse the input string for compare attri.
 * @str: string to be parsed, eg: "0x00 0x11 0x11".
 * @addr: store the phy addr. eg: 0x00.
 * @reg: store the reg addr. eg: 0x11.
 * @val: store the value. eg: 0x11.
 *
 * return 0 if success, otherwise failed.
 */
static int sunxi_parse_write_str(char *str, u16 *addr,
					u16 *reg, u16 *val)
{
	u16 result_addr[3] = { 0 };
	char *ptr = str;
	char *ptr2 = NULL;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(result_addr); i++) {
		ptr = skip_spaces(ptr);
		ptr2 = strchr(ptr, ' ');
		if (ptr2)
			*ptr2 = '\0';

		ret = kstrtou16(ptr, 16, &result_addr[i]);

		if (!ptr2 || ret)
			break;

		ptr = ptr2 + 1;
	}

	*addr = result_addr[0];
	*reg = result_addr[1];
	*val = result_addr[2];

	return ret;
}

static ssize_t sunxi_dwmac_mii_read_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_dwmac *chip = priv->plat->bsp_priv;

	if (!netif_running(ndev)) {
		dev_err(dev, "Error: eth is not running\n");
		return 0;
	}

	chip->mii_reg.value = priv->mii->read(priv->mii, chip->mii_reg.addr, chip->mii_reg.reg);
	return sprintf(buf, "ADDR[0x%02x]:REG[0x%02x] = 0x%04x\n",
				chip->mii_reg.addr, chip->mii_reg.reg, chip->mii_reg.value);
}

static ssize_t sunxi_dwmac_mii_read_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_dwmac *chip = priv->plat->bsp_priv;
	int ret;
	u16 reg, addr;
	char *ptr;

	ptr = (char *)buf;

	if (!netif_running(ndev)) {
		dev_err(dev, "Error: eth is not running\n");
		return count;
	}

	ret = sunxi_parse_read_str(ptr, &addr, &reg);
	if (ret)
		return ret;

	chip->mii_reg.addr = addr;
	chip->mii_reg.reg = reg;

	return count;
}
static DEVICE_ATTR(mii_read, 0664, sunxi_dwmac_mii_read_show, sunxi_dwmac_mii_read_store);

static ssize_t sunxi_dwmac_mii_write_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_dwmac *chip = priv->plat->bsp_priv;
	u16 bef_val, aft_val;

	if (!netif_running(ndev)) {
		dev_err(dev, "Error: eth is not running\n");
		return 0;
	}

	bef_val = priv->mii->read(priv->mii, chip->mii_reg.addr, chip->mii_reg.reg);
	priv->mii->write(priv->mii, chip->mii_reg.addr, chip->mii_reg.reg, chip->mii_reg.value);
	aft_val = priv->mii->read(priv->mii, chip->mii_reg.addr, chip->mii_reg.reg);
	return sprintf(buf, "before ADDR[0x%02x]:REG[0x%02x] = 0x%04x\n"
				"after  ADDR[0x%02x]:REG[0x%02x] = 0x%04x\n",
				chip->mii_reg.addr, chip->mii_reg.reg, bef_val,
				chip->mii_reg.addr, chip->mii_reg.reg, aft_val);
}

static ssize_t sunxi_dwmac_mii_write_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_dwmac *chip = priv->plat->bsp_priv;
	int ret;
	u16 reg, addr, val;
	char *ptr;

	ptr = (char *)buf;

	if (!netif_running(ndev)) {
		dev_err(dev, "Error: eth is not running\n");
		return count;
	}

	ret = sunxi_parse_write_str(ptr, &addr, &reg, &val);
	if (ret)
		return ret;

	chip->mii_reg.reg = reg;
	chip->mii_reg.addr = addr;
	chip->mii_reg.value = val;

	return count;
}
static DEVICE_ATTR(mii_write, 0664, sunxi_dwmac_mii_write_show, sunxi_dwmac_mii_write_store);

static ssize_t sunxi_dwmac_tx_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_dwmac *chip = priv->plat->bsp_priv;
	u32 reg_val;
	u16 tx_delay;

	reg_val = readl(chip->syscfg_base);
	tx_delay = (reg_val & DWMAC_SYSCON_ETXDC_MASK) >> DWMAC_SYSCON_ETXDC_SHIFT;

	return sprintf(buf, "Usage:\necho [0~7] > tx_delay\n"
			"\nnow tx_delay: %d\n", tx_delay);
}

static ssize_t sunxi_dwmac_tx_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_dwmac *chip = priv->plat->bsp_priv;
	int ret;
	u32 reg_val;
	u16 tx_delay;

	if (!netif_running(ndev)) {
		dev_err(dev, "Error: eth is not running\n");
		return count;
	}

	ret = kstrtou16(buf, 0, &tx_delay);
	if (ret)
		return ret;

	if (tx_delay > chip->variant->tx_delay_max) {
		dev_err(dev, "Error: tx_delay exceed max %d\n", chip->variant->tx_delay_max);
		return -EINVAL;
	}

	reg_val = readl(chip->syscfg_base);
	reg_val &= ~DWMAC_SYSCON_ETXDC_MASK;
	reg_val |= tx_delay << DWMAC_SYSCON_ETXDC_SHIFT;
	writel(reg_val, chip->syscfg_base);

	return count;
}
static DEVICE_ATTR(tx_delay, 0664, sunxi_dwmac_tx_delay_show, sunxi_dwmac_tx_delay_store);

static ssize_t sunxi_dwmac_rx_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_dwmac *chip = priv->plat->bsp_priv;
	u32 reg_val;
	u16 rx_delay;

	reg_val = readl(chip->syscfg_base);
	rx_delay = (reg_val & DWMAC_SYSCON_ERXDC_MASK) >> DWMAC_SYSCON_ERXDC_SHIFT;

	return sprintf(buf, "Usage:\necho [0~31] > rx_delay\n"
			"\nnow rx_delay: %d\n", rx_delay);
}

static ssize_t sunxi_dwmac_rx_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_dwmac *chip = priv->plat->bsp_priv;
	int ret;
	u32 reg_val;
	u16 rx_delay;

	if (!netif_running(ndev)) {
		dev_err(dev, "Error: eth is not running\n");
		return count;
	}

	ret = kstrtou16(buf, 0, &rx_delay);
	if (ret)
		return ret;

	if (rx_delay > chip->variant->rx_delay_max) {
		dev_err(dev, "Error: rx_delay exceed max %d\n", chip->variant->rx_delay_max);
		return -EINVAL;
	}

	reg_val = readl(chip->syscfg_base);
	reg_val &= ~DWMAC_SYSCON_ERXDC_MASK;
	reg_val |= rx_delay << DWMAC_SYSCON_ERXDC_SHIFT;
	writel(reg_val, chip->syscfg_base);

	return count;
}
static DEVICE_ATTR(rx_delay, 0664, sunxi_dwmac_rx_delay_show, sunxi_dwmac_rx_delay_store);

static void sunxi_dwmac_sysfs_init(struct device *dev)
{
	device_create_file(dev, &dev_attr_tx_delay);
	device_create_file(dev, &dev_attr_rx_delay);
	device_create_file(dev, &dev_attr_mii_read);
	device_create_file(dev, &dev_attr_mii_write);
}

static void sunxi_dwmac_sysfs_exit(struct device *dev)
{
	device_remove_file(dev, &dev_attr_tx_delay);
	device_remove_file(dev, &dev_attr_rx_delay);
	device_remove_file(dev, &dev_attr_mii_read);
	device_remove_file(dev, &dev_attr_mii_write);
}

#ifndef MODULE
static void sunxi_dwmac_set_mac(u8 *dst, u8 *src)
{
	int i;
	char *p = src;

	for (i = 0; i < ETH_ALEN; i++, p++)
		dst[i] = simple_strtoul(p, &p, 16);
}
#endif

static int sunxi_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct sunxi_dwmac *chip;
	struct device *dev = &pdev->dev;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&pdev->dev, "Error: Alloc sunxi dwmac err\n");
		return -ENOMEM;
	}

	chip->variant = of_device_get_match_data(&pdev->dev);
	if (!chip->variant) {
		dev_err(&pdev->dev, "Error: Missing dwmac-sunxi variant\n");
		return -EINVAL;
	}

	chip->dev = dev;

	ret = sunxi_dwmac_resource_get(pdev, chip);
	if (ret < 0)
		return -EINVAL;

	plat_dat = stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

#ifdef MODULE
	get_custom_mac_address(1, "eth", stmmac_res.mac);
#else
	sunxi_dwmac_set_mac(stmmac_res.mac, mac_str);
#endif

	plat_dat->bsp_priv = chip;
	plat_dat->init = sunxi_dwmac_init;
	plat_dat->exit = sunxi_dwmac_exit;
	/* must use 0~4G space */
	plat_dat->addr64 = 32;
	chip->interface = plat_dat->interface;

	ret = sunxi_dwmac_init(pdev, plat_dat->bsp_priv);
	if (ret)
		goto err_init;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_dvr_probe;

	sunxi_dwmac_sysfs_init(&pdev->dev);

	return 0;

err_dvr_probe:
	sunxi_dwmac_exit(pdev, chip);
err_init:
	stmmac_remove_config_dt(pdev, plat_dat);
	return ret;
}

static int sunxi_dwmac_remove(struct platform_device *pdev)
{
	stmmac_pltfr_remove(pdev);
	sunxi_dwmac_sysfs_exit(&pdev->dev);

	return 0;
}

static void sunxi_dwmac_shutdown(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct sunxi_dwmac *chip = priv->plat->bsp_priv;

	sunxi_dwmac_exit(pdev, chip);
}

static const struct of_device_id sunxi_dwmac_match[] = {
	{ .compatible = "allwinner,sunxi-gmac-200", .data = &dwmac200_variant },
	{ }
};
MODULE_DEVICE_TABLE(of, sunxi_dwmac_match);

static struct platform_driver sunxi_dwmac_driver = {
	.probe = sunxi_dwmac_probe,
	.remove = sunxi_dwmac_remove,
	.shutdown = sunxi_dwmac_shutdown,
	.driver = {
		.name			= "dwmac-sunxi",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = sunxi_dwmac_match,
	},
};
module_platform_driver(sunxi_dwmac_driver);

#ifndef MODULE
static int __init sunxi_dwmac_set_mac_addr(char *str)
{
	char *p = str;

	if (str && strlen(str))
		memcpy(mac_str, p, MAC_ADDR_LEN);

	return 0;
}
__setup("mac_addr=", sunxi_dwmac_set_mac_addr);
#endif /* MODULE */

MODULE_DESCRIPTION("Allwinner DWMAC driver");
MODULE_AUTHOR("wujiayi <wujiayi@allwinnertech.com>");
MODULE_AUTHOR("xuminghui <xuminghui@allwinnertech.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DWMAC_MODULE_VERSION);

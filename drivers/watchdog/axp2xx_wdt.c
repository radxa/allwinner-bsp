// SPDX-License-Identifier: GPL-2.0-only
/*
 * Watchdog driver for X-Powers AXP2101-family PMICs (AXP318W / AXP8191).
 *
 * Binds the "x-powers,axp2xx-watchdog" MFD cell registered by the AW
 * axp2101 MFD core and drives it over the parent MFD regmap.
 *
 * Single control register REG 0x77 (AXP8191_WATCHDOG_CFG), verified against
 * AXP318W datasheet V0.1 sec 6.9 / 6.11.2.81:
 *   [7]   module enable      (1 = on)
 *   [6]   module clock enable(1 = on)
 *   [5:4] timeout action     11b = RESTART (PMIC power off then on)
 *   [3]   timer clear / feed (write 1, auto-clears)
 *   [2:0] timeout period     0..7 = 1,2,4,8,16,32,64,128 s
 *
 * RESTART mode gives a full PMIC power-cycle that the SoC (sunxi) watchdog
 * cannot do -- it resets the PMIC rails, not just the SoC.
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

#define AXP_WDT_CFG_REG		0x77
#define AXP_WDT_EN		BIT(7)
#define AXP_WDT_CLK_EN		BIT(6)
#define AXP_WDT_MODE_MASK	GENMASK(5, 4)
#define AXP_WDT_MODE_RESTART	(0x3 << 4)
#define AXP_WDT_CLR		BIT(3)
#define AXP_WDT_TIMEOUT_MASK	GENMASK(2, 0)

#define AXP_WDT_MIN_TIMEOUT	1
#define AXP_WDT_MAX_TIMEOUT	128
#define AXP_WDT_DEF_TIMEOUT	16

/* register value (0..7) -> timeout in seconds */
static const unsigned int axp_wdt_timeout[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct axp_wdt {
	struct watchdog_device wdt;
	struct regmap *regmap;
};

/* smallest register step >= requested seconds */
static int axp_wdt_sel(unsigned int timeout)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(axp_wdt_timeout); i++)
		if (axp_wdt_timeout[i] >= timeout)
			return i;

	return ARRAY_SIZE(axp_wdt_timeout) - 1;
}

static int axp_wdt_set_timeout(struct watchdog_device *wdd,
			       unsigned int timeout)
{
	struct axp_wdt *w = watchdog_get_drvdata(wdd);
	int sel = axp_wdt_sel(timeout);
	int ret;

	ret = regmap_update_bits(w->regmap, AXP_WDT_CFG_REG,
				 AXP_WDT_TIMEOUT_MASK, sel);
	if (ret)
		return ret;

	wdd->timeout = axp_wdt_timeout[sel];
	return 0;
}

static int axp_wdt_ping(struct watchdog_device *wdd)
{
	struct axp_wdt *w = watchdog_get_drvdata(wdd);

	return regmap_update_bits(w->regmap, AXP_WDT_CFG_REG,
				  AXP_WDT_CLR, AXP_WDT_CLR);
}

static int axp_wdt_start(struct watchdog_device *wdd)
{
	struct axp_wdt *w = watchdog_get_drvdata(wdd);
	int ret;

	/* full PMIC power-cycle on timeout */
	ret = regmap_update_bits(w->regmap, AXP_WDT_CFG_REG,
				 AXP_WDT_MODE_MASK, AXP_WDT_MODE_RESTART);
	if (ret)
		return ret;

	ret = axp_wdt_set_timeout(wdd, wdd->timeout);
	if (ret)
		return ret;

	ret = axp_wdt_ping(wdd);
	if (ret)
		return ret;

	return regmap_update_bits(w->regmap, AXP_WDT_CFG_REG,
				  AXP_WDT_EN | AXP_WDT_CLK_EN,
				  AXP_WDT_EN | AXP_WDT_CLK_EN);
}

static int axp_wdt_stop(struct watchdog_device *wdd)
{
	struct axp_wdt *w = watchdog_get_drvdata(wdd);

	return regmap_update_bits(w->regmap, AXP_WDT_CFG_REG, AXP_WDT_EN, 0);
}

static const struct watchdog_info axp_wdt_info = {
	.identity = "axp2xx pmic watchdog",
	.options  = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops axp_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= axp_wdt_start,
	.stop		= axp_wdt_stop,
	.ping		= axp_wdt_ping,
	.set_timeout	= axp_wdt_set_timeout,
};

static int axp_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct axp_wdt *w;

	w = devm_kzalloc(dev, sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;

	w->regmap = dev_get_regmap(dev->parent, NULL);
	if (!w->regmap)
		return -ENODEV;

	w->wdt.info		= &axp_wdt_info;
	w->wdt.ops		= &axp_wdt_ops;
	w->wdt.min_timeout	= AXP_WDT_MIN_TIMEOUT;
	w->wdt.max_timeout	= AXP_WDT_MAX_TIMEOUT;
	w->wdt.timeout		= AXP_WDT_DEF_TIMEOUT;
	w->wdt.parent		= dev;

	watchdog_set_drvdata(&w->wdt, w);
	watchdog_init_timeout(&w->wdt, 0, dev);
	watchdog_set_nowayout(&w->wdt, nowayout);

	/* keep disabled until userspace opens the device */
	regmap_update_bits(w->regmap, AXP_WDT_CFG_REG, AXP_WDT_EN, 0);

	return devm_watchdog_register_device(dev, &w->wdt);
}

static const struct of_device_id axp_wdt_of_match[] = {
	{ .compatible = "x-powers,axp2xx-watchdog" },
	{ }
};
MODULE_DEVICE_TABLE(of, axp_wdt_of_match);

static struct platform_driver axp_wdt_driver = {
	.probe	= axp_wdt_probe,
	.driver	= {
		.name		= "axp2xx-watchdog",
		.of_match_table	= axp_wdt_of_match,
	},
};
module_platform_driver(axp_wdt_driver);

MODULE_DESCRIPTION("X-Powers AXP2101-family PMIC watchdog");
MODULE_AUTHOR("Radxa AIOT");
MODULE_LICENSE("GPL");

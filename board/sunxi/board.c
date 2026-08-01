// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2012-2013 Henrik Nordstrom <henrik@henriknordstrom.net>
 * (C) Copyright 2013 Luke Kenneth Casson Leighton <lkcl@lkcl.net>
 *
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Tom Cubie <tangliang@allwinnertech.com>
 *
 * Some board init for the Allwinner A10-evb board.
 */

#include <common.h>
#include <command.h>
#include <dm.h>
#include <env.h>
#include <hang.h>
#include <image.h>
#include <init.h>
#include <log.h>
#include <mmc.h>
#include <axp_pmic.h>
#include <generic-phy.h>
#include <phy-sun4i-usb.h>
#include <asm/arch/clock.h>
#include <asm/arch/cpu.h>
#include <asm/arch/display.h>
#include <asm/arch/dram.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mmc.h>
#include <asm/arch/spl.h>
#include <linux/delay.h>
#include <u-boot/crc.h>
#ifndef CONFIG_ARM64
#include <asm/armv7.h>
#endif
#include <asm/gpio.h>
#include <asm/io.h>
#include <u-boot/crc.h>
#include <env_internal.h>
#include <linux/libfdt.h>
#include <nand.h>
#include <net.h>
#include <spl.h>
#include <sy8106a.h>
#include <asm/setup.h>
#if defined(CONFIG_MACH_SUNIV) && defined(CONFIG_SUNIV_OLED)
#include <i2c.h>
#include <linux/errno.h>
#endif
#if defined(CONFIG_MACH_SUNIV) && defined(CONFIG_SUNIV_DIRECT_SPI_BOOT)
#include <spi_flash.h>
#endif

#if defined(CONFIG_MACH_SUNIV) && defined(CONFIG_SUNIV_OLED)
#define OLED_WIDTH	128
#define OLED_HEIGHT	64
#define OLED_PAGES	(OLED_HEIGHT / 8)
#define OLED_ADDR	0x3C

static bool oled_inited;

static int oled_i2c_write(u8 control, const u8 *data, size_t len)
{
	return i2c_write(OLED_ADDR, control, 1, (u8 *)data, len);
}

static int suniv_i2c_set_hwadapnr(int hwadapnr)
{
	int i;

	for (i = 0; i < CONFIG_SYS_NUM_I2C_BUSES; i++) {
		if (i2c_get_adapter(i)->hwadapnr == hwadapnr)
			return i2c_set_bus_num(i);
	}

	return -ENODEV;
}

static void oled_set_pixel(u8 *buf, int x, int y)
{
	int page;
	int bit;

	if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT)
		return;

	page = y >> 3;
	bit = y & 0x7;
	buf[page * OLED_WIDTH + x] |= (1 << bit);
}

struct oled_glyph {
	char ch;
	u8 col[5];
};

static const struct oled_glyph oled_font[] = {
	{ ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } },
	{ '.', { 0x00, 0x60, 0x60, 0x00, 0x00 } },
	{ '0', { 0x3e, 0x51, 0x49, 0x45, 0x3e } },
	{ '1', { 0x00, 0x42, 0x7f, 0x40, 0x00 } },
	{ '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } },
	{ '3', { 0x21, 0x41, 0x45, 0x4b, 0x31 } },
	{ '4', { 0x18, 0x14, 0x12, 0x7f, 0x10 } },
	{ '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
	{ '6', { 0x3c, 0x4a, 0x49, 0x49, 0x30 } },
	{ '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
	{ '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
	{ '9', { 0x06, 0x49, 0x49, 0x29, 0x1e } },
	{ 'A', { 0x7e, 0x11, 0x11, 0x11, 0x7e } },
	{ 'B', { 0x7f, 0x49, 0x49, 0x49, 0x36 } },
	{ 'D', { 0x7f, 0x41, 0x41, 0x22, 0x1c } },
	{ 'F', { 0x7f, 0x09, 0x09, 0x09, 0x01 } },
	{ 'G', { 0x3e, 0x41, 0x49, 0x49, 0x7a } },
	{ 'I', { 0x00, 0x41, 0x7f, 0x41, 0x00 } },
	{ 'K', { 0x7f, 0x08, 0x14, 0x22, 0x41 } },
	{ 'L', { 0x7f, 0x40, 0x40, 0x40, 0x40 } },
	{ 'M', { 0x7f, 0x02, 0x0c, 0x02, 0x7f } },
	{ 'N', { 0x7f, 0x04, 0x08, 0x10, 0x7f } },
	{ 'O', { 0x3e, 0x41, 0x41, 0x41, 0x3e } },
	{ 'P', { 0x7f, 0x09, 0x09, 0x09, 0x06 } },
	{ 'R', { 0x7f, 0x09, 0x19, 0x29, 0x46 } },
	{ 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } },
	{ 'T', { 0x01, 0x01, 0x7f, 0x01, 0x01 } },
	{ 'U', { 0x3f, 0x40, 0x40, 0x40, 0x3f } },
};

static const u8 *oled_find_glyph(char c)
{
	int i;

	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';

	for (i = 0; i < ARRAY_SIZE(oled_font); i++) {
		if (oled_font[i].ch == c)
			return oled_font[i].col;
	}

	return oled_font[0].col;
}

static void oled_draw_char(u8 *buf, int x, int y, char c)
{
	const u8 *glyph = oled_find_glyph(c);
	int row;
	int col;

	for (col = 0; col < 5; col++) {
		u8 bits = glyph[col];
		for (row = 0; row < 7; row++) {
			if (bits & (1 << row))
				oled_set_pixel(buf, x + col, y + row);
		}
	}
}

static void oled_draw_text(u8 *buf, int row, const char *text)
{
	int x = 0;
	int i;

	for (i = 0; i < (OLED_WIDTH / 6) && text[i]; i++) {
		oled_draw_char(buf, x, row * 8, text[i]);
		x += 6;
	}
}

static int suniv_oled_init(void)
{
	const u8 init_cmds[] = {
		0xAE,       /* display off */
		0xD5, 0x80, /* clock divide */
		0xA8, 0x3F, /* multiplex 1/64 */
		0xD3, 0x00, /* display offset */
		0x40,       /* start line */
		0x8D, 0x14, /* charge pump */
		0x20, 0x02, /* page addressing mode */
		0xA1,       /* segment remap (mirror) */
		0xC0,       /* COM scan dir (normal) */
		0xDA, 0x12, /* COM pins */
		0x81, 0x7F, /* contrast */
		0xD9, 0xF1, /* pre-charge */
		0xDB, 0x40, /* VCOM detect */
		0xA4,       /* display follows RAM */
		0xA6,       /* normal display */
		0xAF,       /* display on */
	};
	int ret;

	if (oled_inited)
		return 0;

	ret = suniv_i2c_set_hwadapnr(CONFIG_SUNIV_OLED_I2C_BUS);
	if (ret) {
		printf("suniv-oled: i2c hw bus %d failed: %d\n",
		       CONFIG_SUNIV_OLED_I2C_BUS, ret);
		return ret;
	}

	ret = oled_i2c_write(0x00, init_cmds, sizeof(init_cmds));
	if (ret) {
		printf("suniv-oled: init cmds write failed: %d\n", ret);
	} else {
		oled_inited = true;
	}

	return ret;
}

static void suniv_oled_show_boot(void)
{
	u8 fb[OLED_WIDTH * OLED_PAGES];
	char dram[16];
	int page;
	int ret;

	ret = suniv_oled_init();
	if (ret) {
		printf("suniv-oled: init failed: %d\n", ret);
		return;
	}

	memset(fb, 0, sizeof(fb));

	sprintf(dram, "DRAM %dM", (int)(gd->ram_size >> 20));
	oled_draw_text(fb, 0, "U-BOOT");
	oled_draw_text(fb, 1, dram);
	oled_draw_text(fb, 2, "SPI NOR");
	oled_draw_text(fb, 3, "BOOTING...");

	for (page = 0; page < OLED_PAGES; page++) {
		u8 cmd[] = {
			(u8)(0xB0 | page),
			0x00,
			0x10,
		};
		ret = oled_i2c_write(0x00, cmd, sizeof(cmd));
		if (ret) {
			printf("suniv-oled: set page %d failed: %d\n",
			       page, ret);
			return;
		}
		ret = oled_i2c_write(0x40, &fb[page * OLED_WIDTH],
				     OLED_WIDTH);
		if (ret) {
			printf("suniv-oled: write page %d failed: %d\n",
			       page, ret);
			return;
		}
	}
}
#endif

#if defined(CONFIG_MACH_SUNIV) && defined(CONFIG_SUNIV_DIRECT_SPI_BOOT)
int last_stage_init(void)
{
	struct cmd_tbl bootz_cmd = {
		.name = "bootz",
	};
	struct spi_flash *flash;
	char *bootz_argv[] = {
		"bootz",
		__stringify(CONFIG_SUNIV_DIRECT_SPI_BOOT_KERNEL_ADDR),
		"-",
		__stringify(CONFIG_SUNIV_DIRECT_SPI_BOOT_FDT_ADDR),
	};
	int ret;

#ifdef CONFIG_SUNIV_OLED
	suniv_oled_show_boot();
#endif

	flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
	if (!flash) {
		printf("suniv-direct: SPI flash probe failed\n");
		hang();
	}

	ret = spi_flash_read(flash, CONFIG_SUNIV_DIRECT_SPI_BOOT_FDT_OFFS,
			     CONFIG_SUNIV_DIRECT_SPI_BOOT_FDT_SIZE,
			     (void *)CONFIG_SUNIV_DIRECT_SPI_BOOT_FDT_ADDR);
	if (ret) {
		printf("suniv-direct: read fdt failed: %d\n", ret);
		hang();
	}

	ret = spi_flash_read(flash, CONFIG_SUNIV_DIRECT_SPI_BOOT_KERNEL_OFFS,
			     CONFIG_SUNIV_DIRECT_SPI_BOOT_KERNEL_SIZE,
			     (void *)CONFIG_SUNIV_DIRECT_SPI_BOOT_KERNEL_ADDR);
	if (ret) {
		printf("suniv-direct: read kernel failed: %d\n", ret);
		hang();
	}

	ret = do_bootz(&bootz_cmd, 0, ARRAY_SIZE(bootz_argv), bootz_argv);
	printf("suniv-direct: bootz returned: %d\n", ret);
	hang();
}
#endif

#if defined CONFIG_VIDEO_LCD_PANEL_I2C && !(defined CONFIG_SPL_BUILD)
/* So that we can use pin names in Kconfig and sunxi_name_to_gpio() */
int soft_i2c_gpio_sda;
int soft_i2c_gpio_scl;

static int soft_i2c_board_init(void)
{
	int ret;

	soft_i2c_gpio_sda = sunxi_name_to_gpio(CONFIG_VIDEO_LCD_PANEL_I2C_SDA);
	if (soft_i2c_gpio_sda < 0) {
		printf("Error invalid soft i2c sda pin: '%s', err %d\n",
		       CONFIG_VIDEO_LCD_PANEL_I2C_SDA, soft_i2c_gpio_sda);
		return soft_i2c_gpio_sda;
	}
	ret = gpio_request(soft_i2c_gpio_sda, "soft-i2c-sda");
	if (ret) {
		printf("Error requesting soft i2c sda pin: '%s', err %d\n",
		       CONFIG_VIDEO_LCD_PANEL_I2C_SDA, ret);
		return ret;
	}

	soft_i2c_gpio_scl = sunxi_name_to_gpio(CONFIG_VIDEO_LCD_PANEL_I2C_SCL);
	if (soft_i2c_gpio_scl < 0) {
		printf("Error invalid soft i2c scl pin: '%s', err %d\n",
		       CONFIG_VIDEO_LCD_PANEL_I2C_SCL, soft_i2c_gpio_scl);
		return soft_i2c_gpio_scl;
	}
	ret = gpio_request(soft_i2c_gpio_scl, "soft-i2c-scl");
	if (ret) {
		printf("Error requesting soft i2c scl pin: '%s', err %d\n",
		       CONFIG_VIDEO_LCD_PANEL_I2C_SCL, ret);
		return ret;
	}

	return 0;
}
#else
static int soft_i2c_board_init(void) { return 0; }
#endif

DECLARE_GLOBAL_DATA_PTR;

void i2c_init_board(void)
{
#ifdef CONFIG_I2C0_ENABLE
#if defined(CONFIG_MACH_SUN4I) || \
    defined(CONFIG_MACH_SUN5I) || \
    defined(CONFIG_MACH_SUN7I) || \
    defined(CONFIG_MACH_SUN8I_R40)
	sunxi_gpio_set_cfgpin(SUNXI_GPB(0), SUN4I_GPB_TWI0);
	sunxi_gpio_set_cfgpin(SUNXI_GPB(1), SUN4I_GPB_TWI0);
	clock_twi_onoff(0, 1);
#elif defined(CONFIG_MACH_SUN6I)
	sunxi_gpio_set_cfgpin(SUNXI_GPH(14), SUN6I_GPH_TWI0);
	sunxi_gpio_set_cfgpin(SUNXI_GPH(15), SUN6I_GPH_TWI0);
	clock_twi_onoff(0, 1);
#elif defined(CONFIG_MACH_SUN8I)
	sunxi_gpio_set_cfgpin(SUNXI_GPH(2), SUN8I_GPH_TWI0);
	sunxi_gpio_set_cfgpin(SUNXI_GPH(3), SUN8I_GPH_TWI0);
	clock_twi_onoff(0, 1);
#elif defined(CONFIG_MACH_SUN50I)
	sunxi_gpio_set_cfgpin(SUNXI_GPH(0), SUN50I_GPH_TWI0);
	sunxi_gpio_set_cfgpin(SUNXI_GPH(1), SUN50I_GPH_TWI0);
	clock_twi_onoff(0, 1);
#endif
#endif

#ifdef CONFIG_I2C1_ENABLE
#if defined(CONFIG_MACH_SUN4I) || \
    defined(CONFIG_MACH_SUN7I) || \
    defined(CONFIG_MACH_SUN8I_R40)
	sunxi_gpio_set_cfgpin(SUNXI_GPB(18), SUN4I_GPB_TWI1);
	sunxi_gpio_set_cfgpin(SUNXI_GPB(19), SUN4I_GPB_TWI1);
	clock_twi_onoff(1, 1);
#elif defined(CONFIG_MACH_SUN5I)
	sunxi_gpio_set_cfgpin(SUNXI_GPB(15), SUN5I_GPB_TWI1);
	sunxi_gpio_set_cfgpin(SUNXI_GPB(16), SUN5I_GPB_TWI1);
	clock_twi_onoff(1, 1);
#elif defined(CONFIG_MACH_SUN6I)
	sunxi_gpio_set_cfgpin(SUNXI_GPH(16), SUN6I_GPH_TWI1);
	sunxi_gpio_set_cfgpin(SUNXI_GPH(17), SUN6I_GPH_TWI1);
	clock_twi_onoff(1, 1);
#elif defined(CONFIG_MACH_SUN8I)
	sunxi_gpio_set_cfgpin(SUNXI_GPH(4), SUN8I_GPH_TWI1);
	sunxi_gpio_set_cfgpin(SUNXI_GPH(5), SUN8I_GPH_TWI1);
	clock_twi_onoff(1, 1);
#elif defined(CONFIG_MACH_SUN50I)
	sunxi_gpio_set_cfgpin(SUNXI_GPH(2), SUN50I_GPH_TWI1);
	sunxi_gpio_set_cfgpin(SUNXI_GPH(3), SUN50I_GPH_TWI1);
	clock_twi_onoff(1, 1);
#endif
#endif

#ifdef CONFIG_I2C2_ENABLE
#if defined(CONFIG_MACH_SUNIV)
	sunxi_gpio_set_cfgpin(SUNXI_GPD(15), SUNIV_GPD_TWI2);
	sunxi_gpio_set_cfgpin(SUNXI_GPD(16), SUNIV_GPD_TWI2);
	clock_twi_onoff(2, 1);
#elif defined(CONFIG_MACH_SUN4I) || \
    defined(CONFIG_MACH_SUN7I) || \
    defined(CONFIG_MACH_SUN8I_R40)
	sunxi_gpio_set_cfgpin(SUNXI_GPB(20), SUN4I_GPB_TWI2);
	sunxi_gpio_set_cfgpin(SUNXI_GPB(21), SUN4I_GPB_TWI2);
	clock_twi_onoff(2, 1);
#elif defined(CONFIG_MACH_SUN5I)
	sunxi_gpio_set_cfgpin(SUNXI_GPB(17), SUN5I_GPB_TWI2);
	sunxi_gpio_set_cfgpin(SUNXI_GPB(18), SUN5I_GPB_TWI2);
	clock_twi_onoff(2, 1);
#elif defined(CONFIG_MACH_SUN6I)
	sunxi_gpio_set_cfgpin(SUNXI_GPH(18), SUN6I_GPH_TWI2);
	sunxi_gpio_set_cfgpin(SUNXI_GPH(19), SUN6I_GPH_TWI2);
	clock_twi_onoff(2, 1);
#elif defined(CONFIG_MACH_SUN8I)
	sunxi_gpio_set_cfgpin(SUNXI_GPE(12), SUN8I_GPE_TWI2);
	sunxi_gpio_set_cfgpin(SUNXI_GPE(13), SUN8I_GPE_TWI2);
	clock_twi_onoff(2, 1);
#elif defined(CONFIG_MACH_SUN50I)
	sunxi_gpio_set_cfgpin(SUNXI_GPE(14), SUN50I_GPE_TWI2);
	sunxi_gpio_set_cfgpin(SUNXI_GPE(15), SUN50I_GPE_TWI2);
	clock_twi_onoff(2, 1);
#endif
#endif

#ifdef CONFIG_I2C3_ENABLE
#if defined(CONFIG_MACH_SUN6I)
	sunxi_gpio_set_cfgpin(SUNXI_GPG(10), SUN6I_GPG_TWI3);
	sunxi_gpio_set_cfgpin(SUNXI_GPG(11), SUN6I_GPG_TWI3);
	clock_twi_onoff(3, 1);
#elif defined(CONFIG_MACH_SUN7I) || \
      defined(CONFIG_MACH_SUN8I_R40)
	sunxi_gpio_set_cfgpin(SUNXI_GPI(0), SUN7I_GPI_TWI3);
	sunxi_gpio_set_cfgpin(SUNXI_GPI(1), SUN7I_GPI_TWI3);
	clock_twi_onoff(3, 1);
#endif
#endif

#ifdef CONFIG_I2C4_ENABLE
#if defined(CONFIG_MACH_SUN7I) || \
    defined(CONFIG_MACH_SUN8I_R40)
	sunxi_gpio_set_cfgpin(SUNXI_GPI(2), SUN7I_GPI_TWI4);
	sunxi_gpio_set_cfgpin(SUNXI_GPI(3), SUN7I_GPI_TWI4);
	clock_twi_onoff(4, 1);
#endif
#endif

#ifdef CONFIG_R_I2C_ENABLE
#ifdef CONFIG_MACH_SUN50I
	clock_twi_onoff(5, 1);
	sunxi_gpio_set_cfgpin(SUNXI_GPL(8), SUN50I_GPL_R_TWI);
	sunxi_gpio_set_cfgpin(SUNXI_GPL(9), SUN50I_GPL_R_TWI);
#else
	clock_twi_onoff(5, 1);
	sunxi_gpio_set_cfgpin(SUNXI_GPL(0), SUN8I_H3_GPL_R_TWI);
	sunxi_gpio_set_cfgpin(SUNXI_GPL(1), SUN8I_H3_GPL_R_TWI);
#endif
#endif
}

#if defined(CONFIG_ENV_IS_IN_MMC) && defined(CONFIG_ENV_IS_IN_FAT)
enum env_location env_get_location(enum env_operation op, int prio)
{
	switch (prio) {
	case 0:
		return ENVL_FAT;

	case 1:
		return ENVL_MMC;

	default:
		return ENVL_UNKNOWN;
	}
}
#endif

#ifdef CONFIG_DM_MMC
static void mmc_pinmux_setup(int sdc);
#endif

/* add board specific code here */
int board_init(void)
{
	__maybe_unused int id_pfr1, ret, satapwr_pin, macpwr_pin;

	gd->bd->bi_boot_params = (PHYS_SDRAM_0 + 0x100);

#if !defined(CONFIG_ARM64) && !defined(CONFIG_MACH_SUNIV)
	asm volatile("mrc p15, 0, %0, c0, c1, 1" : "=r"(id_pfr1));
	debug("id_pfr1: 0x%08x\n", id_pfr1);
	/* Generic Timer Extension available? */
	if ((id_pfr1 >> CPUID_ARM_GENTIMER_SHIFT) & 0xf) {
		uint32_t freq;

		debug("Setting CNTFRQ\n");

		/*
		 * CNTFRQ is a secure register, so we will crash if we try to
		 * write this from the non-secure world (read is OK, though).
		 * In case some bootcode has already set the correct value,
		 * we avoid the risk of writing to it.
		 */
		asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(freq));
		if (freq != COUNTER_FREQUENCY) {
			debug("arch timer frequency is %d Hz, should be %d, fixing ...\n",
			      freq, COUNTER_FREQUENCY);
#ifdef CONFIG_NON_SECURE
			printf("arch timer frequency is wrong, but cannot adjust it\n");
#else
			asm volatile("mcr p15, 0, %0, c14, c0, 0"
				     : : "r"(COUNTER_FREQUENCY));
#endif
		}
	}
#endif /* !CONFIG_ARM64 && !CONFIG_MACH_SUNIV */

	ret = axp_gpio_init();
	if (ret)
		return ret;

#ifdef CONFIG_SATAPWR
	satapwr_pin = sunxi_name_to_gpio(CONFIG_SATAPWR);
	gpio_request(satapwr_pin, "satapwr");
	gpio_direction_output(satapwr_pin, 1);
	/* Give attached sata device time to power-up to avoid link timeouts */
	mdelay(500);
#endif
#ifdef CONFIG_MACPWR
	macpwr_pin = sunxi_name_to_gpio(CONFIG_MACPWR);
	gpio_request(macpwr_pin, "macpwr");
	gpio_direction_output(macpwr_pin, 1);
#endif

#if defined(CONFIG_DM_I2C) || \
    (defined(CONFIG_MACH_SUNIV) && defined(CONFIG_SUNIV_OLED))
	/*
	 * Temporary workaround for enabling I2C clocks until proper sunxi DM
	 * clk, reset and pinctrl drivers land.
	 */
	i2c_init_board();
#endif

#ifdef CONFIG_DM_MMC
	/*
	 * Temporary workaround for enabling MMC clocks until a sunxi DM
	 * pinctrl driver lands.
	 */
	mmc_pinmux_setup(CONFIG_MMC_SUNXI_SLOT);
#if CONFIG_MMC_SUNXI_SLOT_EXTRA != -1
	mmc_pinmux_setup(CONFIG_MMC_SUNXI_SLOT_EXTRA);
#endif
#endif	/* CONFIG_DM_MMC */

	/* Uses dm gpio code so do this here and not in i2c_init_board() */
	return soft_i2c_board_init();
}

/*
 * On older SoCs the SPL is actually at address zero, so using NULL as
 * an error value does not work.
 */
#define INVALID_SPL_HEADER ((void *)~0UL)

static struct boot_file_head * get_spl_header(uint8_t req_version)
{
	struct boot_file_head *spl = (void *)(ulong)SPL_ADDR;
	uint8_t spl_header_version = spl->spl_signature[3];

	/* Is there really the SPL header (still) there? */
	if (memcmp(spl->spl_signature, SPL_SIGNATURE, 3) != 0)
		return INVALID_SPL_HEADER;

	if (spl_header_version < req_version) {
		printf("sunxi SPL version mismatch: expected %u, got %u\n",
		       req_version, spl_header_version);
		return INVALID_SPL_HEADER;
	}

	return spl;
}

int dram_init(void)
{
	struct boot_file_head *spl = get_spl_header(SPL_DRAM_HEADER_VERSION);

	if (spl == INVALID_SPL_HEADER)
		gd->ram_size = get_ram_size((long *)PHYS_SDRAM_0,
					    PHYS_SDRAM_0_SIZE);
	else
		gd->ram_size = (phys_addr_t)spl->dram_size << 20;

	if (gd->ram_size > CONFIG_SUNXI_DRAM_MAX_SIZE)
		gd->ram_size = CONFIG_SUNXI_DRAM_MAX_SIZE;

	return 0;
}

#if defined(CONFIG_NAND_SUNXI)
static void nand_pinmux_setup(void)
{
	unsigned int pin;

	for (pin = SUNXI_GPC(0); pin <= SUNXI_GPC(19); pin++)
		sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_NAND);

#if defined CONFIG_MACH_SUN4I || defined CONFIG_MACH_SUN7I
	for (pin = SUNXI_GPC(20); pin <= SUNXI_GPC(22); pin++)
		sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_NAND);
#endif
	/* sun4i / sun7i do have a PC23, but it is not used for nand,
	 * only sun7i has a PC24 */
#ifdef CONFIG_MACH_SUN7I
	sunxi_gpio_set_cfgpin(SUNXI_GPC(24), SUNXI_GPC_NAND);
#endif
}

static void nand_clock_setup(void)
{
	struct sunxi_ccm_reg *const ccm =
		(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;

	setbits_le32(&ccm->ahb_gate0, (CLK_GATE_OPEN << AHB_GATE_OFFSET_NAND0));
#if defined CONFIG_MACH_SUN6I || defined CONFIG_MACH_SUN8I || \
    defined CONFIG_MACH_SUN9I || defined CONFIG_MACH_SUN50I
	setbits_le32(&ccm->ahb_reset0_cfg, (1 << AHB_GATE_OFFSET_NAND0));
#endif
	setbits_le32(&ccm->nand0_clk_cfg, CCM_NAND_CTRL_ENABLE | AHB_DIV_1);
}

void board_nand_init(void)
{
	nand_pinmux_setup();
	nand_clock_setup();
#ifndef CONFIG_SPL_BUILD
	sunxi_nand_init();
#endif
}
#endif

#ifdef CONFIG_MMC
static void mmc_pinmux_setup(int sdc)
{
	__maybe_unused unsigned int pin;
	__maybe_unused int pins;

	switch (sdc) {
	case 0:
		/* SDC0: PF0-PF5 */
#ifndef CONFIG_UART0_PORT_F
		for (pin = SUNXI_GPF(0); pin <= SUNXI_GPF(5); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPF_SDC0);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
#endif
		break;

	case 1:
		pins = sunxi_name_to_gpio_bank(CONFIG_MMC1_PINS);

#if defined(CONFIG_MACH_SUN4I) || defined(CONFIG_MACH_SUN7I) || \
    defined(CONFIG_MACH_SUN8I_R40)
		if (pins == SUNXI_GPIO_H) {
			/* SDC1: PH22-PH-27 */
			for (pin = SUNXI_GPH(22); pin <= SUNXI_GPH(27); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUN4I_GPH_SDC1);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}
		} else {
			/* SDC1: PG0-PG5 */
			for (pin = SUNXI_GPG(0); pin <= SUNXI_GPG(5); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUN4I_GPG_SDC1);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}
		}
#elif defined(CONFIG_MACH_SUN5I)
		/* SDC1: PG3-PG8 */
		for (pin = SUNXI_GPG(3); pin <= SUNXI_GPG(8); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUN5I_GPG_SDC1);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
#elif defined(CONFIG_MACH_SUN6I)
		/* SDC1: PG0-PG5 */
		for (pin = SUNXI_GPG(0); pin <= SUNXI_GPG(5); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUN6I_GPG_SDC1);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
#elif defined(CONFIG_MACH_SUN8I)
		if (pins == SUNXI_GPIO_D) {
			/* SDC1: PD2-PD7 */
			for (pin = SUNXI_GPD(2); pin <= SUNXI_GPD(7); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUN8I_GPD_SDC1);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}
		} else {
			/* SDC1: PG0-PG5 */
			for (pin = SUNXI_GPG(0); pin <= SUNXI_GPG(5); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUN8I_GPG_SDC1);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}
		}
#endif
		break;

	case 2:
		pins = sunxi_name_to_gpio_bank(CONFIG_MMC2_PINS);

#if defined(CONFIG_MACH_SUN4I) || defined(CONFIG_MACH_SUN7I)
		/* SDC2: PC6-PC11 */
		for (pin = SUNXI_GPC(6); pin <= SUNXI_GPC(11); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
#elif defined(CONFIG_MACH_SUN5I)
		if (pins == SUNXI_GPIO_E) {
			/* SDC2: PE4-PE9 */
			for (pin = SUNXI_GPE(4); pin <= SUNXI_GPD(9); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUN5I_GPE_SDC2);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}
		} else {
			/* SDC2: PC6-PC15 */
			for (pin = SUNXI_GPC(6); pin <= SUNXI_GPC(15); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}
		}
#elif defined(CONFIG_MACH_SUN6I)
		if (pins == SUNXI_GPIO_A) {
			/* SDC2: PA9-PA14 */
			for (pin = SUNXI_GPA(9); pin <= SUNXI_GPA(14); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUN6I_GPA_SDC2);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}
		} else {
			/* SDC2: PC6-PC15, PC24 */
			for (pin = SUNXI_GPC(6); pin <= SUNXI_GPC(15); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}

			sunxi_gpio_set_cfgpin(SUNXI_GPC(24), SUNXI_GPC_SDC2);
			sunxi_gpio_set_pull(SUNXI_GPC(24), SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(SUNXI_GPC(24), 2);
		}
#elif defined(CONFIG_MACH_SUN8I_R40)
		/* SDC2: PC6-PC15, PC24 */
		for (pin = SUNXI_GPC(6); pin <= SUNXI_GPC(15); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}

		sunxi_gpio_set_cfgpin(SUNXI_GPC(24), SUNXI_GPC_SDC2);
		sunxi_gpio_set_pull(SUNXI_GPC(24), SUNXI_GPIO_PULL_UP);
		sunxi_gpio_set_drv(SUNXI_GPC(24), 2);
#elif defined(CONFIG_MACH_SUN8I) || defined(CONFIG_MACH_SUN50I)
		/* SDC2: PC5-PC6, PC8-PC16 */
		for (pin = SUNXI_GPC(5); pin <= SUNXI_GPC(6); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}

		for (pin = SUNXI_GPC(8); pin <= SUNXI_GPC(16); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
#elif defined(CONFIG_MACH_SUN50I_H6)
		/* SDC2: PC4-PC14 */
		for (pin = SUNXI_GPC(4); pin <= SUNXI_GPC(14); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
#elif defined(CONFIG_MACH_SUN9I)
		/* SDC2: PC6-PC16 */
		for (pin = SUNXI_GPC(6); pin <= SUNXI_GPC(16); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPC_SDC2);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
#endif
		break;

	case 3:
		pins = sunxi_name_to_gpio_bank(CONFIG_MMC3_PINS);

#if defined(CONFIG_MACH_SUN4I) || defined(CONFIG_MACH_SUN7I) || \
    defined(CONFIG_MACH_SUN8I_R40)
		/* SDC3: PI4-PI9 */
		for (pin = SUNXI_GPI(4); pin <= SUNXI_GPI(9); pin++) {
			sunxi_gpio_set_cfgpin(pin, SUNXI_GPI_SDC3);
			sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(pin, 2);
		}
#elif defined(CONFIG_MACH_SUN6I)
		if (pins == SUNXI_GPIO_A) {
			/* SDC3: PA9-PA14 */
			for (pin = SUNXI_GPA(9); pin <= SUNXI_GPA(14); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUN6I_GPA_SDC3);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}
		} else {
			/* SDC3: PC6-PC15, PC24 */
			for (pin = SUNXI_GPC(6); pin <= SUNXI_GPC(15); pin++) {
				sunxi_gpio_set_cfgpin(pin, SUN6I_GPC_SDC3);
				sunxi_gpio_set_pull(pin, SUNXI_GPIO_PULL_UP);
				sunxi_gpio_set_drv(pin, 2);
			}

			sunxi_gpio_set_cfgpin(SUNXI_GPC(24), SUN6I_GPC_SDC3);
			sunxi_gpio_set_pull(SUNXI_GPC(24), SUNXI_GPIO_PULL_UP);
			sunxi_gpio_set_drv(SUNXI_GPC(24), 2);
		}
#endif
		break;

	default:
		printf("sunxi: invalid MMC slot %d for pinmux setup\n", sdc);
		break;
	}
}

int board_mmc_init(bd_t *bis)
{
	__maybe_unused struct mmc *mmc0, *mmc1;

	mmc_pinmux_setup(CONFIG_MMC_SUNXI_SLOT);
	mmc0 = sunxi_mmc_init(CONFIG_MMC_SUNXI_SLOT);
	if (!mmc0)
		return -1;

#if CONFIG_MMC_SUNXI_SLOT_EXTRA != -1
	mmc_pinmux_setup(CONFIG_MMC_SUNXI_SLOT_EXTRA);
	mmc1 = sunxi_mmc_init(CONFIG_MMC_SUNXI_SLOT_EXTRA);
	if (!mmc1)
		return -1;
#endif

	return 0;
}
#endif

#ifdef CONFIG_SPL_BUILD

static void sunxi_spl_store_dram_size(phys_addr_t dram_size)
{
	struct boot_file_head *spl = get_spl_header(SPL_DT_HEADER_VERSION);

	if (spl == INVALID_SPL_HEADER)
		return;

	/* Promote the header version for U-Boot proper, if needed. */
	if (spl->spl_signature[3] < SPL_DRAM_HEADER_VERSION)
		spl->spl_signature[3] = SPL_DRAM_HEADER_VERSION;

	spl->dram_size = dram_size >> 20;
}

void sunxi_board_init(void)
{
	int power_failed = 0;

#ifdef CONFIG_SY8106A_POWER
	power_failed = sy8106a_set_vout1(CONFIG_SY8106A_VOUT1_VOLT);
#endif

#if defined CONFIG_AXP152_POWER || defined CONFIG_AXP209_POWER || \
	defined CONFIG_AXP221_POWER || defined CONFIG_AXP809_POWER || \
	defined CONFIG_AXP818_POWER
	power_failed = axp_init();

#if defined CONFIG_AXP221_POWER || defined CONFIG_AXP809_POWER || \
	defined CONFIG_AXP818_POWER
	power_failed |= axp_set_dcdc1(CONFIG_AXP_DCDC1_VOLT);
#endif
	power_failed |= axp_set_dcdc2(CONFIG_AXP_DCDC2_VOLT);
	power_failed |= axp_set_dcdc3(CONFIG_AXP_DCDC3_VOLT);
#if !defined(CONFIG_AXP209_POWER) && !defined(CONFIG_AXP818_POWER)
	power_failed |= axp_set_dcdc4(CONFIG_AXP_DCDC4_VOLT);
#endif
#if defined CONFIG_AXP221_POWER || defined CONFIG_AXP809_POWER || \
	defined CONFIG_AXP818_POWER
	power_failed |= axp_set_dcdc5(CONFIG_AXP_DCDC5_VOLT);
#endif

#if defined CONFIG_AXP221_POWER || defined CONFIG_AXP809_POWER || \
	defined CONFIG_AXP818_POWER
	power_failed |= axp_set_aldo1(CONFIG_AXP_ALDO1_VOLT);
#endif
	power_failed |= axp_set_aldo2(CONFIG_AXP_ALDO2_VOLT);
#if !defined(CONFIG_AXP152_POWER)
	power_failed |= axp_set_aldo3(CONFIG_AXP_ALDO3_VOLT);
#endif
#ifdef CONFIG_AXP209_POWER
	power_failed |= axp_set_aldo4(CONFIG_AXP_ALDO4_VOLT);
#endif

#if defined(CONFIG_AXP221_POWER) || defined(CONFIG_AXP809_POWER) || \
	defined(CONFIG_AXP818_POWER)
	power_failed |= axp_set_dldo(1, CONFIG_AXP_DLDO1_VOLT);
	power_failed |= axp_set_dldo(2, CONFIG_AXP_DLDO2_VOLT);
#if !defined CONFIG_AXP809_POWER
	power_failed |= axp_set_dldo(3, CONFIG_AXP_DLDO3_VOLT);
	power_failed |= axp_set_dldo(4, CONFIG_AXP_DLDO4_VOLT);
#endif
	power_failed |= axp_set_eldo(1, CONFIG_AXP_ELDO1_VOLT);
	power_failed |= axp_set_eldo(2, CONFIG_AXP_ELDO2_VOLT);
	power_failed |= axp_set_eldo(3, CONFIG_AXP_ELDO3_VOLT);
#endif

#ifdef CONFIG_AXP818_POWER
	power_failed |= axp_set_fldo(1, CONFIG_AXP_FLDO1_VOLT);
	power_failed |= axp_set_fldo(2, CONFIG_AXP_FLDO2_VOLT);
	power_failed |= axp_set_fldo(3, CONFIG_AXP_FLDO3_VOLT);
#endif

#if defined CONFIG_AXP809_POWER || defined CONFIG_AXP818_POWER
	power_failed |= axp_set_sw(IS_ENABLED(CONFIG_AXP_SW_ON));
#endif
#endif
	printf("DRAM:");
	gd->ram_size = sunxi_dram_init();
	printf(" %d MiB\n", (int)(gd->ram_size >> 20));
	if (!gd->ram_size)
		hang();

	sunxi_spl_store_dram_size(gd->ram_size);

	/*
	 * Only clock up the CPU to full speed if we are reasonably
	 * assured it's being powered with suitable core voltage
	 */
	if (!power_failed)
		clock_set_pll1(CONFIG_SYS_CLK_FREQ);
	else
		printf("Failed to set core voltage! Can't set CPU frequency\n");
}
#endif

#ifdef CONFIG_USB_GADGET
int g_dnl_board_usb_cable_connected(void)
{
	struct udevice *dev;
	struct phy phy;
	int ret;

	ret = uclass_get_device(UCLASS_USB_GADGET_GENERIC, 0, &dev);
	if (ret) {
		pr_err("%s: Cannot find USB device\n", __func__);
		return ret;
	}

	ret = generic_phy_get_by_name(dev, "usb", &phy);
	if (ret) {
		pr_err("failed to get %s USB PHY\n", dev->name);
		return ret;
	}

	ret = generic_phy_init(&phy);
	if (ret) {
		pr_err("failed to init %s USB PHY\n", dev->name);
		return ret;
	}

	ret = sun4i_usb_phy_vbus_detect(&phy);
	if (ret == 1) {
		pr_err("A charger is plugged into the OTG\n");
		return -ENODEV;
	}

	return ret;
}
#endif

#ifdef CONFIG_SERIAL_TAG
void get_board_serial(struct tag_serialnr *serialnr)
{
	char *serial_string;
	unsigned long long serial;

	serial_string = env_get("serial#");

	if (serial_string) {
		serial = simple_strtoull(serial_string, NULL, 16);

		serialnr->high = (unsigned int) (serial >> 32);
		serialnr->low = (unsigned int) (serial & 0xffffffff);
	} else {
		serialnr->high = 0;
		serialnr->low = 0;
	}
}
#endif

/*
 * Check the SPL header for the "sunxi" variant. If found: parse values
 * that might have been passed by the loader ("fel" utility), and update
 * the environment accordingly.
 */
static void parse_spl_header(const uint32_t spl_addr)
{
	struct boot_file_head *spl = get_spl_header(SPL_ENV_HEADER_VERSION);

	if (spl == INVALID_SPL_HEADER)
		return;

	if (!spl->fel_script_address)
		return;

	if (spl->fel_uEnv_length != 0) {
		/*
		 * data is expected in uEnv.txt compatible format, so "env
		 * import -t" the string(s) at fel_script_address right away.
		 */
		himport_r(&env_htab, (char *)(uintptr_t)spl->fel_script_address,
			  spl->fel_uEnv_length, '\n', H_NOCLEAR, 0, 0, NULL);
		return;
	}
	/* otherwise assume .scr format (mkimage-type script) */
	env_set_hex("fel_scriptaddr", spl->fel_script_address);
}

/*
 * Note this function gets called multiple times.
 * It must not make any changes to env variables which already exist.
 */
static void setup_environment(const void *fdt)
{
	char serial_string[17] = { 0 };
	unsigned int sid[4];
	uint8_t mac_addr[6];
	char ethaddr[16];
	int i, ret;

	ret = sunxi_get_sid(sid);
	if (ret == 0 && sid[0] != 0) {
		/*
		 * The single words 1 - 3 of the SID have quite a few bits
		 * which are the same on many models, so we take a crc32
		 * of all 3 words, to get a more unique value.
		 *
		 * Note we only do this on newer SoCs as we cannot change
		 * the algorithm on older SoCs since those have been using
		 * fixed mac-addresses based on only using word 3 for a
		 * long time and changing a fixed mac-address with an
		 * u-boot update is not good.
		 */
#if !defined(CONFIG_MACH_SUN4I) && !defined(CONFIG_MACH_SUN5I) && \
    !defined(CONFIG_MACH_SUN6I) && !defined(CONFIG_MACH_SUN7I) && \
    !defined(CONFIG_MACH_SUN8I_A23) && !defined(CONFIG_MACH_SUN8I_A33)
		sid[3] = crc32(0, (unsigned char *)&sid[1], 12);
#endif

		/* Ensure the NIC specific bytes of the mac are not all 0 */
		if ((sid[3] & 0xffffff) == 0)
			sid[3] |= 0x800000;

		for (i = 0; i < 4; i++) {
			sprintf(ethaddr, "ethernet%d", i);
			if (!fdt_get_alias(fdt, ethaddr))
				continue;

			if (i == 0)
				strcpy(ethaddr, "ethaddr");
			else
				sprintf(ethaddr, "eth%daddr", i);

			if (env_get(ethaddr))
				continue;

			/* Non OUI / registered MAC address */
			mac_addr[0] = (i << 4) | 0x02;
			mac_addr[1] = (sid[0] >>  0) & 0xff;
			mac_addr[2] = (sid[3] >> 24) & 0xff;
			mac_addr[3] = (sid[3] >> 16) & 0xff;
			mac_addr[4] = (sid[3] >>  8) & 0xff;
			mac_addr[5] = (sid[3] >>  0) & 0xff;

			eth_env_set_enetaddr(ethaddr, mac_addr);
		}

		if (!env_get("serial#")) {
			snprintf(serial_string, sizeof(serial_string),
				"%08x%08x", sid[0], sid[3]);

			env_set("serial#", serial_string);
		}
	}
}

int misc_init_r(void)
{
	uint boot;

	env_set("fel_booted", NULL);
	env_set("fel_scriptaddr", NULL);
	env_set("mmc_bootdev", NULL);

	boot = sunxi_get_boot_device();
	/* determine if we are running in FEL mode */
	if (boot == BOOT_DEVICE_BOARD) {
		env_set("fel_booted", "1");
		parse_spl_header(SPL_ADDR);
	/* or if we booted from MMC, and which one */
	} else if (boot == BOOT_DEVICE_MMC1) {
		env_set("mmc_bootdev", "0");
	} else if (boot == BOOT_DEVICE_MMC2) {
		env_set("mmc_bootdev", "1");
	}

	setup_environment(gd->fdt_blob);

#if defined(CONFIG_MACH_SUNIV) && defined(CONFIG_SUNIV_OLED)
	suniv_oled_show_boot();
#endif

#ifdef CONFIG_USB_ETHER
	usb_ether_init();
#endif

	return 0;
}

int ft_board_setup(void *blob, bd_t *bd)
{
	int __maybe_unused r;

	/*
	 * Call setup_environment again in case the boot fdt has
	 * ethernet aliases the u-boot copy does not have.
	 */
	setup_environment(blob);

#ifdef CONFIG_VIDEO_DT_SIMPLEFB
	r = sunxi_simplefb_setup(blob);
	if (r)
		return r;
#endif
	return 0;
}

#ifdef CONFIG_SPL_LOAD_FIT
int board_fit_config_name_match(const char *name)
{
	struct boot_file_head *spl = get_spl_header(SPL_DT_HEADER_VERSION);
	const char *cmp_str = (const char *)spl;

	/* Check if there is a DT name stored in the SPL header and use that. */
	if (spl != INVALID_SPL_HEADER && spl->dt_name_offset) {
		cmp_str += spl->dt_name_offset;
	} else {
#ifdef CONFIG_DEFAULT_DEVICE_TREE
		cmp_str = CONFIG_DEFAULT_DEVICE_TREE;
#else
		return 0;
#endif
	};

#ifdef CONFIG_PINE64_DT_SELECTION
/* Differentiate the two Pine64 board DTs by their DRAM size. */
	if (strstr(name, "-pine64") && strstr(cmp_str, "-pine64")) {
		if ((gd->ram_size > 512 * 1024 * 1024))
			return !strstr(name, "plus");
		else
			return !!strstr(name, "plus");
	} else {
		return strcmp(name, cmp_str);
	}
#endif
	return strcmp(name, cmp_str);
}
#endif

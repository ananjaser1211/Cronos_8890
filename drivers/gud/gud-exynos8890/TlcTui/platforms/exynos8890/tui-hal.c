/*
 * Copyright (c) 2014-2015 TRUSTONIC LIMITED
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/pm_runtime.h>
#include <linux/exynos_ion.h>
#include <linux/dma-buf.h>
#include <linux/ion.h>
#include <t-base-tui.h>
#include <linux/ktime.h>
#include <linux/switch.h>
#include "../../../../../video/fbdev/exynos/decon_8890/decon.h"
#include "tui_ioctl.h"
#include "dciTui.h"
#include "tlcTui.h"
#include "tui-hal.h"
#if defined(CONFIG_TOUCHSCREEN_ZINITIX_ZT75XX) || defined(CONFIG_TOUCHSCREEN_ZINITIX_ZT75XX_TCLM)
extern void trustedui_mode_on(void);
extern void trustedui_mode_off(void);
#elif defined(CONFIG_TOUCHSCREEN_IST3038H)
extern void trustedui_mode_ist_on(void);
extern void trustedui_mode_ist_off(void);
#endif

/* I2C register for reset */
#define HSI2C7_PA_BASE_ADDRESS	0x14E10000
#define HSI2C_CTL		0x00
#define HSI2C_TRAILIG_CTL	0x08
#define HSI2C_FIFO_STAT		0x30
#define HSI2C_CONF		0x40
#define HSI2C_TRANS_STATUS	0x50

#define HSI2C_SW_RST		(1u << 31)
#define HSI2C_FUNC_MODE_I2C	(1u << 0)
#define HSI2C_MASTER		(1u << 3)
#define HSI2C_TRAILING_COUNT	(0xf)
#define HSI2C_AUTO_MODE		(1u << 31)
#define HSI2C_RX_FIFO_EMPTY	(1u << 24)
#define HSI2C_TX_FIFO_EMPTY	(1u << 8)
#define HSI2C_FIFO_EMPTY	(HSI2C_RX_FIFO_EMPTY | HSI2C_TX_FIFO_EMPTY)
#define TUI_MEMPOOL_SIZE 0

extern struct switch_dev tui_switch;

extern phys_addr_t hal_tui_video_space_alloc(void);
extern int decon_lpd_block_exit(struct decon_device *decon);

/* for ion_map mapping on smmu */
extern struct ion_device *ion_exynos;
/* ------------end ---------- */

#ifdef CONFIG_TRUSTED_UI_TOUCH_ENABLE
static int tsp_irq_num = 11; // default value

static void tui_delay(unsigned int ms)
{
	if (ms < 20)
		usleep_range(ms * 1000, ms * 1000);
	else
		msleep(ms);
}

void trustedui_set_tsp_irq(int irq_num)
{
	tsp_irq_num = irq_num;
	pr_info("%s called![%d]\n",__func__, irq_num);
}
#endif

struct tui_mempool {
	void *va;
	phys_addr_t pa;
	size_t size;
};

static struct tui_mempool g_tuiMemPool;

static struct ion_client *client;
#define COUNT_OF_ION_HANDLE (4)
static struct ion_handle *handle[COUNT_OF_ION_HANDLE];


static bool allocateTuiMemoryPool(struct tui_mempool *pool, size_t size)
{
	bool ret = false;
	void *tuiMemPool = NULL;

	pr_info("%s %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
	if (!size) {
		pr_debug("TUI frame buffer: nothing to allocate.");
		return true;
	}

	tuiMemPool = kmalloc(size, GFP_KERNEL);
	if (!tuiMemPool) {
		pr_debug("ERROR Could not allocate TUI memory pool");
	}
	else if (ksize(tuiMemPool) < size) {
		pr_debug("ERROR TUI memory pool allocated size is too small. required=%zd allocated=%zd", size, ksize(tuiMemPool));
		kfree(tuiMemPool);
	}
	else {
		pool->va = tuiMemPool;
		pool->pa = virt_to_phys(tuiMemPool);
		pool->size = ksize(tuiMemPool);
		ret = true;
	}
	return ret;
}

static void freeTuiMemoryPool(struct tui_mempool *pool)
{
	if(pool->va) {
	kfree(pool->va);
	memset(pool, 0, sizeof(*pool));
}
}

void hold_i2c_clock(void)
{
	struct clk *touch_i2c_pclk;
	touch_i2c_pclk = clk_get(NULL, "i2c2_pclk");
	if (IS_ERR(touch_i2c_pclk)) {
		pr_err("Can't get [i2c2_pclk]\n");
	}

	clk_prepare_enable(touch_i2c_pclk);

	pr_info("[i2c2_pclk] will be enabled\n");
	clk_put(touch_i2c_pclk);
}

void release_i2c_clock(void)
{
	struct clk *touch_i2c_pclk;
	touch_i2c_pclk = clk_get(NULL, "i2c2_pclk");
	if (IS_ERR(touch_i2c_pclk)) {
		pr_err("Can't get [i2c2_pclk]\n");
	}

	clk_disable_unprepare(touch_i2c_pclk);

	pr_info("[i2c2_pclk] will be disabled\n");

	clk_put(touch_i2c_pclk);
}

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI_FB_BLANK
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,9,0)
static int is_device_ok(struct device *fbdev, void *p)
#else
static int is_device_ok(struct device *fbdev, const void *p)
#endif
{
	return 1;
}
#endif

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI_FB_BLANK
static struct device *get_fb_dev(void)
{
	struct device *fbdev = NULL;

	/* get the first framebuffer device */
	/* [TODO] Handle properly when there are more than one framebuffer */
	fbdev = class_find_device(fb_class, NULL, NULL, is_device_ok);
	if (NULL == fbdev) {
		pr_debug("ERROR cannot get framebuffer device\n");
		return NULL;
	}
	return fbdev;
}
#endif

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI_FB_BLANK
static struct fb_info *get_fb_info(struct device *fbdev)
{
	struct fb_info *fb_info;

	if (!fbdev->p) {
		pr_debug("ERROR framebuffer device has no private data\n");
		return NULL;
	}

	fb_info = (struct fb_info *)dev_get_drvdata(fbdev);
	if (!fb_info) {
		pr_debug("ERROR framebuffer device has no fb_info\n");
		return NULL;
	}

	return fb_info;
}
#endif

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI_FB_BLANK
static int fb_tui_protection(void)
{
	struct device *fbdev = NULL;
	struct fb_info *fb_info;
	struct decon_win *win;
	struct decon_device *decon;
	int ret = -ENODEV;	

	fbdev = get_fb_dev();
	if (!fbdev) {
		pr_debug("get_fb_dev failed\n");
		return ret;
	}

	fb_info = get_fb_info(fbdev);
	if (!fb_info) {
		pr_debug("get_fb_info failed\n");
		return ret;
	}

	win = fb_info->par;
	decon = win->decon;

	lock_fb_info(fb_info);
	// FIX ME !! //
	ret = decon_tui_protection(decon, true);
	unlock_fb_info(fb_info);

	return ret;
}
#endif

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI_FB_BLANK
static int fb_tui_unprotection(void)
{
	struct device *fbdev = NULL;
	struct fb_info *fb_info;
	struct decon_win *win;
	struct decon_device *decon;
	int ret = -ENODEV;

	fbdev = get_fb_dev();
	if (!fbdev) {
		pr_debug("get_fb_dev failed\n");
		return ret;
	}

	fb_info = get_fb_info(fbdev);
	if (!fb_info) {
		printk("get_fb_info failed\n");
		return ret;
	}

	win = fb_info->par;
	if (win == NULL) {
		printk("get win failed\n");
		return ret;
	}

	decon = win->decon;
	if (decon == NULL) {
		printk("get decon failed\n");
		return ret;
	}

	/*
	if (decon->pdata->trig_mode == DECON_HW_TRIG)
	decon_reg_set_trigger(decon->id, decon->pdata->dsi_mode,
	decon->pdata->trig_mode, DECON_TRIG_ENABLE);
	 */
	lock_fb_info(fb_info);
	// FIX ME //
	ret = decon_tui_protection(decon, false);
	unlock_fb_info(fb_info);

	return ret;
}
#endif

uint32_t hal_tui_init(void)
{
	/* Allocate memory pool for the framebuffer
	 */
	if (!allocateTuiMemoryPool(&g_tuiMemPool, TUI_MEMPOOL_SIZE)) {
		return TUI_DCI_ERR_INTERNAL_ERROR;
	}

	return TUI_DCI_OK;
}

void hal_tui_exit(void)
{
	/* delete memory pool if any */
	if (g_tuiMemPool.va) {
		freeTuiMemoryPool(&g_tuiMemPool);
	}
}

uint32_t hal_tui_alloc(tuiAllocBuffer_t *allocbuffer, size_t allocsize, uint32_t count)
{
	int ret = TUI_DCI_ERR_INTERNAL_ERROR;
	ion_phys_addr_t phys_addr;
	//unsigned long offset = 0;
	size_t size;
	size_t phy_size = 0;
	int i = 0;

	size=allocsize*(count+1);

	client = ion_client_create(ion_exynos, "TUI module");
	if (client == NULL) {
		printk("[%s:%d] ION client creation fail.\n", __func__, __LINE__);
		return ret;
	}
	
	for (i = 0; i < count; i++) {
		handle[i] = ion_alloc(client, SZ_8M, SZ_256K,
				EXYNOS_ION_HEAP_VIDEO_FRAME_MASK, 0);
		if (IS_ERR_OR_NULL(handle[i])) {
			pr_err("[%s:%d] ION memory allocation fail.[err:%lx]\n", __func__, __LINE__, PTR_ERR(handle[i]));
			hal_tui_free();
			return ret;
		}
		ion_phys(client, handle[i], (unsigned long *)&phys_addr, &phy_size);
		allocbuffer[i].pa = (uint64_t)phys_addr;
		pr_info("[%s:%d] TUI buffer alloc idx:%d\n", __func__, __LINE__, i);
	}

		ret = TUI_DCI_OK;

		return ret;
}


#ifdef CONFIG_TRUSTED_UI_TOUCH_ENABLE
void tui_i2c_reset(void)
{
	void __iomem *i2c_reg;
	u32 tui_i2c;
	u32 i2c_conf;

	i2c_reg = ioremap(HSI2C7_PA_BASE_ADDRESS, SZ_4K);
	tui_i2c = readl(i2c_reg + HSI2C_CTL);
	tui_i2c |= HSI2C_SW_RST;
	writel(tui_i2c, i2c_reg + HSI2C_CTL);

	tui_i2c = readl(i2c_reg + HSI2C_CTL);
	tui_i2c &= ~HSI2C_SW_RST;
	writel(tui_i2c, i2c_reg + HSI2C_CTL);

	writel(0x4c4c4c00, i2c_reg + 0x0060);
	writel(0x26004c4c, i2c_reg + 0x0064);
	writel(0x99, i2c_reg + 0x0068);

	i2c_conf = readl(i2c_reg + HSI2C_CONF);
	writel((HSI2C_FUNC_MODE_I2C | HSI2C_MASTER), i2c_reg + HSI2C_CTL);

	writel(HSI2C_TRAILING_COUNT, i2c_reg + HSI2C_TRAILIG_CTL);
	writel(i2c_conf | HSI2C_AUTO_MODE, i2c_reg + HSI2C_CONF);

	iounmap(i2c_reg);
}
#endif

void hal_tui_free(void)
{
	int i;

	for (i = 0; i < COUNT_OF_ION_HANDLE; i++) {
		if (!IS_ERR_OR_NULL(handle[i])){
			pr_info("[%s:%d] TUI buffer free idx:%d\n", __func__, __LINE__, i);
			ion_free(client, handle[i]);
			handle[i] = NULL;
		}
	}

	ion_client_destroy(client);
}

uint32_t hal_tui_deactivate(void)
{
	int ret = TUI_DCI_OK;

	switch_set_state(&tui_switch, TRUSTEDUI_MODE_VIDEO_SECURED);
	pr_info(KERN_ERR "Disable touch!\n");
	disable_irq(tsp_irq_num);

#if defined(CONFIG_TOUCHSCREEN_ZINITIX_ZT75XX) || defined(CONFIG_TOUCHSCREEN_ZINITIX_ZT75XX_TCLM)
		tui_delay(5);
		/*esd timer disable*/
		trustedui_mode_on();
		tui_delay(95);
#elif defined(CONFIG_TOUCHSCREEN_IST3038H)
		tui_delay(5);
		/*esd timer disable*/
		trustedui_mode_ist_on();
		tui_delay(95);
#else
		tui_delay(1);
#endif

	pr_info(KERN_ERR "tsp_irq_num =%d\n",tsp_irq_num);

	/* Set linux TUI flag */
	trustedui_set_mask(TRUSTEDUI_MODE_TUI_SESSION);
	trustedui_blank_set_counter(0);

	hold_i2c_clock();

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI_FB_BLANK
	pr_info(KERN_ERR "blanking!\n");
	ret = fb_tui_protection();
	if(ret != 0){
		pr_info(KERN_ERR "blanking ERR! fb_tui_protection ret = %d\n", ret);

		//TSP enable
		switch_set_state(&tui_switch, TRUSTEDUI_MODE_OFF);
		enable_irq(tsp_irq_num);
		release_i2c_clock();
		
#if defined(CONFIG_TOUCHSCREEN_ZINITIX_ZT75XX) || defined(CONFIG_TOUCHSCREEN_ZINITIX_ZT75XX_TCLM)
		tui_delay(5);
		/*esd timer enable*/
		trustedui_mode_off();
		tui_delay(95);
#elif defined(CONFIG_TOUCHSCREEN_IST3038H)
		tui_delay(5);
		/*esd timer enable*/
		trustedui_mode_ist_off();
		tui_delay(95);
#endif

		/* Clear linux TUI flag */
		trustedui_set_mode(TRUSTEDUI_MODE_OFF);

		return TUI_DCI_ERR_INTERNAL_ERROR;
		}
#endif // #CONFIG_TRUSTONIC_TRUSTED_UI_FB_BLANK

	trustedui_set_mask(TRUSTEDUI_MODE_VIDEO_SECURED|TRUSTEDUI_MODE_INPUT_SECURED);

	pr_info(KERN_ERR "Ready to use TUI!\n");

	return TUI_DCI_OK;
}

uint32_t hal_tui_activate(void)
{
	int ret = TUI_DCI_OK;

	// Protect NWd
	trustedui_clear_mask(TRUSTEDUI_MODE_VIDEO_SECURED|TRUSTEDUI_MODE_INPUT_SECURED);

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI_FB_BLANK
	pr_info("Unblanking\n");
	ret = fb_tui_unprotection();
	if(ret != 0){
		pr_info(KERN_ERR "unblanking ERR! fb_tui_unprotection ret = %d retry!!!\n", ret);
		tui_delay(500);
		ret = fb_tui_unprotection();
		if(ret == -ENODEV)
			pr_info(KERN_ERR "unblanking retry ERR!!! fb_tui_unprotection ret = %d\n", ret);
	}	
#endif

	switch_set_state(&tui_switch, TRUSTEDUI_MODE_OFF);
//	tui_i2c_reset();
	enable_irq(tsp_irq_num);
	release_i2c_clock();

#if defined(CONFIG_TOUCHSCREEN_ZINITIX_ZT75XX) || defined(CONFIG_TOUCHSCREEN_ZINITIX_ZT75XX_TCLM)
	tui_delay(5);
	/*esd timer enable*/
	trustedui_mode_off();
	tui_delay(95);
#elif defined(CONFIG_TOUCHSCREEN_IST3038H)
	tui_delay(5);
	/*esd timer enable*/
	trustedui_mode_ist_off();
	tui_delay(95);
#endif

	/* Clear linux TUI flag */
	trustedui_set_mode(TRUSTEDUI_MODE_OFF);

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI_FB_BLANK
	pr_info("Unsetting TUI flag (blank counter=%d)", trustedui_blank_get_counter());

if (0 < trustedui_blank_get_counter()) {
	//		blank_framebuffer(0);
}
#endif
	pr_info(KERN_ERR "Closed TUI session.\n");

	return TUI_DCI_OK;
}


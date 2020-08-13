#
# Makefile for the <t-base trusted UI driver
#
PLATFORM := exynos8890

GUD_ROOT_FOLDER := drivers/gud/gud-exynos8890

# add our modules to kernel.
obj-$(CONFIG_TRUSTONIC_TRUSTED_UI) += TlcTui.o

TlcTui-y := main.o tlcTui.o trustedui.o	\
	platforms/$(PLATFORM)/tui-hal.o

# Release mode by default
ccflags-y += -DNDEBUG
ccflags-y += -Wno-declaration-after-statement

ccflags-$(CONFIG_TRUSTONIC_TEE_DEBUG) += -DDEBUG

ccflags-y += -I$(GUD_ROOT_FOLDER)/TlcTui
ccflags-y += -Idrivers/video/fbdev/exynos/decon_8890

# MobiCore Driver includes
ccflags-y += -I$(GUD_ROOT_FOLDER)/MobiCoreDriver/public

# MobiCore TlcTui required includes
ccflags-y += -I$(GUD_ROOT_FOLDER)/TlcTui/inc \
             -I$(GUD_ROOT_FOLDER)/TlcTui/public

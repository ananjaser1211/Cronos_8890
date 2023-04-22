# SPDX-License-Identifier: GPL-2.0

dtb-$(CONFIG_MODEL_GRACELTE) += exynos8890-gracelte_eur_open_00.dtb exynos8890-gracelte_eur_open_05.dtb exynos8890-gracelte_eur_open_09.dtb exynos8890-gracelte_eur_open_11.dtb
dtb-$(CONFIG_MODEL_HERO2LTE) += exynos8890-hero2lte_eur_open_04.dtb exynos8890-hero2lte_eur_open_08.dtb
dtb-$(CONFIG_MODEL_HEROLTE) += exynos8890-herolte_eur_open_04.dtb exynos8890-herolte_eur_open_08.dtb exynos8890-herolte_eur_open_09.dtb

targets += dtbs

DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif
targets += $(DTB_LIST)

ifeq ($(CONFIG_EXYNOS_DTBTOOL),y)
targets += dtb.img

DTB_OBJS += $(shell find -L $(obj)/ -name \*.dtb)

$(obj)/dtb.img: $(DTB_OBJS) FORCE
	$(call if_changed,dtbtool_exynos)
endif


dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := dts/*.dtb *.dtb

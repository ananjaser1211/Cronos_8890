#!/bin/bash
#
# Cronos Build Script V4.1
# For Exynos8890
# Coded by AnanJaser1211 @2019
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software

# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Main Dir
CR_DIR=$(pwd)
# Define toolchan path
CR_TC=~/Android/Toolchains/aarch64-linux-android-4.9/bin/aarch64-linux-android-
# Define proper arch and dir for dts files
CR_DTS=arch/arm64/boot/dts
CR_DTS_TREBLE=arch/arm64/boot/exynos8890_Treble.dtsi
CR_DTS_ONEUI=arch/arm64/boot/exynos8890_Oneui.dtsi
# Define boot.img out dir
CR_OUT=$CR_DIR/Cronos/Out
CR_PRODUCT=$CR_DIR/Cronos/Product
# Presistant A.I.K Location
CR_AIK=$CR_DIR/Cronos/A.I.K
# Main Ramdisk Location
CR_RAMDISK=$CR_DIR/Cronos/Ramdisk
CR_RAMDISK_Q=$CR_DIR/Cronos/Q
# Compiled image name and location (Image/zImage)
CR_KERNEL=$CR_DIR/arch/arm64/boot/Image
# Compiled dtb by dtbtool
CR_DTB=$CR_DIR/boot.img-dtb
# Kernel Name and Version
CR_VERSION=V5.0
CR_NAME=CronosKernel
# Thread count
CR_JOBS=$(nproc --all)
# Target Android version
CR_ANDROID=p
CR_PLATFORM=9.0.0
# Target ARCH
CR_ARCH=arm64
# Current Date
CR_DATE=$(date +%Y%m%d)
# Init build
export CROSS_COMPILE=$CR_TC
# General init
export ANDROID_MAJOR_VERSION=$CR_ANDROID
export PLATFORM_VERSION=$CR_PLATFORM
export $CR_ARCH
##########################################
# Device specific Variables [SM-G930X]
CR_DTSFILES_G930="exynos8890-herolte_eur_open_08.dtb exynos8890-herolte_eur_open_09.dtb exynos8890-herolte_eur_open_10.dtb"
CR_CONFIG_G930=hero_defconfig
CR_VARIANT_G930=G930X
# Device specific Variables [SM-G935X]
CR_DTSFILES_G935="exynos8890-hero2lte_eur_open_00.dtb exynos8890-hero2lte_eur_open_01.dtb exynos8890-hero2lte_eur_open_03.dtb exynos8890-hero2lte_eur_open_04.dtb exynos8890-hero2lte_eur_open_08.dtb"
CR_CONFIG_G935=hero2_defconfig
CR_VARIANT_G935=G935X
# Device specific Variables [SM-N935X]
CR_DTSFILES_N935="exynos8890-gracelte_eur_open_00.dtb exynos8890-gracelte_eur_open_01.dtb exynos8890-gracelte_eur_open_02.dtb exynos8890-gracelte_eur_open_03.dtb exynos8890-gracelte_eur_open_05.dtb exynos8890-gracelte_eur_open_07.dtb exynos8890-gracelte_eur_open_09.dtb exynos8890-gracelte_eur_open_11.dtb"
CR_CONFIG_N935=gracer_defconfig
CR_VARIANT_N935=N935X
# Common configs
CR_CONFIG_TREBLE=treble_defconfig
CR_CONFIG_ONEUI=oneui_defconfig
CR_CONFIG_8890=exynos8890_defconfig
CR_CONFIG_SPLIT=NULL
CR_CONFIG_CRONOS=cronos_defconfig
CR_ROOT="0"
CR_PERMISSIVE="0"
CR_HALLIC="0"
CR_BOMB="0"
#####################################################

# Script functions

read -p "Clean source (y/n) > " yn
if [ "$yn" = "Y" -o "$yn" = "y" ]; then
     echo "Clean Build"
     CR_CLEAN="1"
else
     echo "Dirty Build"
     CR_CLEAN="0"
fi

# Treble / OneUI
read -p "Variant? (1 (OneUI-Treble) | 2 (AOSP-Treble) | 3 (OneUI) | 4 (OneUI Q) > " aud
if [ "$aud" = "1" ]; then
     echo "Build OneUI-Treble Variant"
     CR_MODE="1"
     CR_PERMISSIVE="1"
fi
if [ "$aud" = "2" ]; then
     echo "Build AOSP-Treble Variant"
     CR_MODE="2"
     CR_PERMISSIVE="1"
fi
if [ "$aud" = "3" ]; then
     echo "Build OneUI Variant"
     CR_MODE="3"
     CR_PERMISSIVE="1"
fi
if [ "$aud" = "4" ]; then
     echo "Build OneUI Q Variant"
     CR_MODE="4"
     CR_PERMISSIVE="1"
     CR_HALLIC="1"
fi

# Got Root?
#read -p "Kernel SU? (y/n) > " yn
#if [ "$yn" = "Y" -o "$yn" = "y" ]; then
#     echo " WARNING : KernelSU Enabled!"
#     export CONFIG_ASSISTED_SUPERUSER=y
#     CR_ROOT="1"
#fi

BUILD_CLEAN()
{
if [ $CR_CLEAN = 1 ]; then
     echo " "
     echo " Cleaning build dir"
     make clean && make mrproper
     rm -r -f $CR_DTB
     rm -rf $CR_DTS/.*.tmp
     rm -rf $CR_DTS/.*.cmd
     rm -rf $CR_DTS/*.dtb
     rm -rf $CR_DIR/.config
     rm -rf $CR_DTS/exynos8890.dtsi
     rm -rf $CR_OUT/*.img
     rm -rf $CR_OUT/*.zip
fi
if [ $CR_CLEAN = 0 ]; then
     echo " "
     echo " Skip Full cleaning"
     rm -r -f $CR_DTB
     rm -rf $CR_DTS/.*.tmp
     rm -rf $CR_DTS/.*.cmd
     rm -rf $CR_DTS/*.dtb
     rm -rf $CR_DIR/.config
     rm -rf $CR_DTS/exynos8890.dtsi
fi
}

BUILD_ROOT()
{
if [ $CR_ROOT = 1 ]; then
     echo " "
     echo " WARNING : KernelSU Enabled!"
     mv $CR_PRODUCT/$CR_IMAGE_NAME.img $CR_PRODUCT/$CR_IMAGE_NAME-KernelSU.img
     CR_IMAGE_NAME=$CR_IMAGE_NAME-KernelSU
fi
}

BUILD_IMAGE_NAME()
{
	CR_IMAGE_NAME=$CR_NAME-$CR_VERSION-$CR_VARIANT-$CR_DATE
}

BUILD_GENERATE_CONFIG()
{
  # Only use for devices that are unified with 2 or more configs
  echo "----------------------------------------------"
	echo " "
	echo "Building defconfig for $CR_VARIANT"
  echo " "
  # Respect CLEAN build rules
  BUILD_CLEAN
  if [ -e $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig ]; then
    echo " cleanup old configs "
    rm -rf $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  echo " Copy $CR_CONFIG "
  cp -f $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  if [ $CR_CONFIG_SPLIT = NULL ]; then
    echo " No split config support! "
  else
    echo " Copy $CR_CONFIG_SPLIT "
    cat $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG_SPLIT >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  if [ $CR_MODE != "NULL" ]; then
    echo " Copy $CR_CONFIG_TYPE "
    cat $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG_TYPE >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  echo " Copy $CR_CONFIG_CRONOS "
  cat $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG_CRONOS >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  if [ $CR_PERMISSIVE = "1" ]; then
    echo " Building Permissive Kernel"
    echo "CONFIG_ALWAYS_PERMISSIVE=y" >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  if [ $CR_HALLIC = "1" ]; then
    echo " Inverting HALL_IC Status"
    echo "CONFIG_HALL_EVENT_REVERSE=y" >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  if [ $CR_BOMB = "1" ]; then
    echo " Legacy BOMB Edition RIL"
	sed -i -- '/CONFIG_MODEM_PIE_REV/d' $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
    echo "# CONFIG_MODEM_PIE_REV is not set" >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  echo " Set $CR_VARIANT to generated config "
  CR_CONFIG=tmp_defconfig
}

BUILD_OUT()
{
  echo " "
  echo "----------------------------------------------"
  echo "$CR_VARIANT kernel build finished."
  echo "Compiled DTB Size = $sizdT Kb"
  echo "Kernel Image Size = $sizT Kb"
  echo "Boot Image   Size = $sizkT Kb"
  echo "$CR_PRODUCT/$CR_IMAGE_NAME.img Ready"
  echo "Press Any key to end the script"
  echo "----------------------------------------------"
}

BUILD_ZIMAGE()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building zImage for $CR_VARIANT"
	export LOCALVERSION=-$CR_IMAGE_NAME
  	cp $CR_DTB_MOUNT $CR_DTS/exynos8890.dtsi
	echo "Make $CR_CONFIG"
	make $CR_CONFIG
	make -j$CR_JOBS
	if [ ! -e $CR_KERNEL ]; then
	exit 0;
	echo "Image Failed to Compile"
	echo " Abort "
	fi
	du -k "$CR_KERNEL" | cut -f1 >sizT
	sizT=$(head -n 1 sizT)
	rm -rf sizT
	echo " "
	echo "----------------------------------------------"
}
BUILD_DTB()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building DTB for $CR_VARIANT"
	# Use the DTS list provided in the build script.
	# This source does not compile dtbs while doing Image
	make $CR_DTSFILES
	./tools/dtbTool/dtbTool -o $CR_DTB -d $CR_DTS/ -s 2048
	if [ ! -e $CR_DTB ]; then
	exit 0;
	echo "DTB Failed to Compile"
	echo " Abort "
	fi
	rm -rf $CR_DTS/.*.tmp
	rm -rf $CR_DTS/.*.cmd
	rm -rf $CR_DTS/*.dtb
	rm -rf $CR_DTS/exynos8890.dtsi
	du -k "$CR_DTB" | cut -f1 >sizdT
	sizdT=$(head -n 1 sizdT)
	rm -rf sizdT
	echo " "
	echo "----------------------------------------------"
}
PACK_BOOT_IMG()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building Boot.img for $CR_VARIANT"
	# Copy Ramdisk
	cp -rf $CR_RAMDISK/* $CR_AIK
	# Move Compiled kernel and dtb to A.I.K Folder
	mv $CR_KERNEL $CR_AIK/split_img/boot.img-zImage
	mv $CR_DTB $CR_AIK/split_img/boot.img-dtb
	# Create boot.img
	$CR_AIK/repackimg.sh
	# Remove red warning at boot
	echo -n "SEANDROIDENFORCE" » $CR_AIK/image-new.img
	# Copy boot.img to Production folder
	cp $CR_AIK/image-new.img $CR_PRODUCT/$CR_IMAGE_NAME.img
	# Move boot.img to out dir
	mv $CR_AIK/image-new.img $CR_OUT/$CR_IMAGE_NAME.img
	du -k "$CR_OUT/$CR_IMAGE_NAME.img" | cut -f1 >sizkT
	sizkT=$(head -n 1 sizkT)
	rm -rf sizkT
	echo " "
	$CR_AIK/cleanup.sh
	# Respect CLEAN build rules
	BUILD_CLEAN
}
# Main Menu
clear
echo "----------------------------------------------"
echo "$CR_NAME $CR_VERSION Build Script"
echo "----------------------------------------------"
PS3='Please select your option (1-4): '
menuvar=("SM-G930X" "SM-G935X" "SM-N935X" "Exit")
select menuvar in "${menuvar[@]}"
do
    case $menuvar in
        "SM-G930X")
            clear
            echo "Starting $CR_VARIANT_G930 kernel build..."
            if [ $CR_MODE = "1" ]; then
              echo " Building Oneui-Treble variant "
              CR_CONFIG_TYPE=$CR_CONFIG_ONEUI
              CR_VARIANT=$CR_VARIANT_G930-TrebleTW
              CR_DTB_MOUNT=$CR_DTS_TREBLE
              CR_RAMDISK=$CR_RAMDISK_Q
            fi
            if [ $CR_MODE = "2" ]; then
              echo " Building AOSP-Treble variant "
              CR_CONFIG_TYPE=$CR_CONFIG_TREBLE
              CR_VARIANT=$CR_VARIANT_G930-Treble
              CR_DTB_MOUNT=$CR_DTS_TREBLE
              CR_RAMDISK=$CR_RAMDISK_Q
            fi
            if [ $CR_MODE = "3" ]; then
              echo " Building OneUI variant "
              CR_CONFIG_TYPE=$CR_CONFIG_ONEUI
              CR_VARIANT=$CR_VARIANT_G930-TW
              CR_DTB_MOUNT=$CR_DTS_ONEUI
            fi
            if [ $CR_MODE = "4" ]; then
              echo " Building Oneui-Q variant "
              CR_CONFIG_TYPE=$CR_CONFIG_ONEUI
              CR_VARIANT=$CR_VARIANT_G930-Q
              CR_DTB_MOUNT=$CR_DTS_ONEUI
              CR_RAMDISK=$CR_RAMDISK_Q
            fi
            CR_CONFIG=$CR_CONFIG_8890
            CR_CONFIG_SPLIT=$CR_CONFIG_G930
            CR_DTSFILES=$CR_DTSFILES_G930
            BUILD_IMAGE_NAME
            BUILD_GENERATE_CONFIG
            BUILD_ZIMAGE
            BUILD_DTB
            PACK_BOOT_IMG
            BUILD_ROOT
            BUILD_OUT
            read -n1 -r key
            break
            ;;
        "SM-G935X")
            clear
            echo "Starting $CR_VARIANT_G935 kernel build..."
            if [ $CR_MODE = "1" ]; then
              echo " Building Oneui-Treble variant "
              CR_CONFIG_TYPE=$CR_CONFIG_ONEUI
              CR_VARIANT=$CR_VARIANT_G935-TrebleTW
              CR_DTB_MOUNT=$CR_DTS_TREBLE
              CR_RAMDISK=$CR_RAMDISK_Q
            fi
            if [ $CR_MODE = "2" ]; then
              echo " Building AOSP-Treble variant "
              CR_CONFIG_TYPE=$CR_CONFIG_TREBLE
              CR_VARIANT=$CR_VARIANT_G935-Treble
              CR_DTB_MOUNT=$CR_DTS_TREBLE
              CR_RAMDISK=$CR_RAMDISK_Q
            fi
            if [ $CR_MODE = "3" ]; then
              echo " Building OneUI variant "
              CR_CONFIG_TYPE=$CR_CONFIG_ONEUI
              CR_VARIANT=$CR_VARIANT_G935-TW
              CR_DTB_MOUNT=$CR_DTS_ONEUI
            fi
            if [ $CR_MODE = "4" ]; then
              echo " Building Oneui-Q variant "
              CR_CONFIG_TYPE=$CR_CONFIG_ONEUI
              CR_VARIANT=$CR_VARIANT_G935-Q
              CR_DTB_MOUNT=$CR_DTS_ONEUI
              CR_RAMDISK=$CR_RAMDISK_Q
            fi
            CR_CONFIG=$CR_CONFIG_8890
            CR_CONFIG_SPLIT=$CR_CONFIG_G935
            CR_DTSFILES=$CR_DTSFILES_G935
            BUILD_IMAGE_NAME
            BUILD_GENERATE_CONFIG
            BUILD_ZIMAGE
            BUILD_DTB
            PACK_BOOT_IMG
            BUILD_ROOT
            BUILD_OUT
            read -n1 -r key
            break
            ;;
        "SM-N935X")
            clear
			read -p "Bomb edition? (y/n) > " yn
			if [ "$yn" = "Y" -o "$yn" = "y" ]; then
				echo " Building Bomb Edition "
				CR_BOMB="1"
				CR_VARIANT_N935=N930X
			else
				echo " Building Fan Edition Edition "
            fi
            echo "Starting $CR_VARIANT_N935 kernel build..."
            if [ $CR_MODE = "1" ]; then
              echo " Building Oneui-Treble variant "
              CR_CONFIG_TYPE=$CR_CONFIG_ONEUI
              CR_VARIANT=$CR_VARIANT_N935-TrebleTW
              CR_DTB_MOUNT=$CR_DTS_TREBLE
              CR_RAMDISK=$CR_RAMDISK_Q
            fi
            if [ $CR_MODE = "2" ]; then
              echo " Building AOSP-Treble variant "
              CR_CONFIG_TYPE=$CR_CONFIG_TREBLE
              CR_VARIANT=$CR_VARIANT_N935-Treble
              CR_DTB_MOUNT=$CR_DTS_TREBLE
              CR_RAMDISK=$CR_RAMDISK_Q
            fi
            if [ $CR_MODE = "3" ]; then
              echo " Building OneUI variant "
              CR_CONFIG_TYPE=$CR_CONFIG_ONEUI
              CR_VARIANT=$CR_VARIANT_N935-TW
              CR_DTB_MOUNT=$CR_DTS_ONEUI
            fi
            if [ $CR_MODE = "4" ]; then
              echo " Building Oneui-Q variant "
              CR_CONFIG_TYPE=$CR_CONFIG_ONEUI
              CR_VARIANT=$CR_VARIANT_N935-Q
              CR_DTB_MOUNT=$CR_DTS_ONEUI
              CR_RAMDISK=$CR_RAMDISK_Q
            fi
            CR_CONFIG=$CR_CONFIG_8890
            CR_CONFIG_SPLIT=$CR_CONFIG_N935
            CR_DTSFILES=$CR_DTSFILES_N935
            BUILD_IMAGE_NAME
            BUILD_GENERATE_CONFIG
            BUILD_ZIMAGE
            BUILD_DTB
            PACK_BOOT_IMG
            BUILD_ROOT
            BUILD_OUT
            read -n1 -r key
            break
            ;;
        "Exit")
            break
            ;;
        *) echo Invalid option.;;
    esac
done

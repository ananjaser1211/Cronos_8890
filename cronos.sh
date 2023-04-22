#!/bin/bash
#
# Cronos Build Script V7.0
# For Exynos8890
# Coded by AnanJaser1211 @ 2019-2023
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
# Define proper arch and dir for dts files
CR_DTS=arch/arm64/boot/dts
CR_DTS_TREBLE=arch/arm64/boot/exynos8890_Treble.dtsi
CR_DTS_ONEUI=arch/arm64/boot/exynos8890_Oneui.dtsi
# Define boot.img out dir
CR_OUT=$CR_DIR/Cronos/Out
CR_PRODUCT=$CR_DIR/Cronos/Product
# Kernel Zip Package
CR_ZIP=$CR_DIR/Cronos/kernelzip
CR_OUTZIP=$CR_OUT/kernelzip
# Presistant A.I.K Location
CR_AIK=$CR_DIR/Cronos/A.I.K
# Main Ramdisk Location
CR_RAMDISK=$CR_DIR/Cronos/Ramdisk
CR_RAMDISK_Q=$CR_DIR/Cronos/Q
# Compiled image name and location (Image/zImage)
CR_KERNEL=$CR_DIR/arch/arm64/boot/Image
# Compiled dtb by dtbtool
CR_DTB=$CR_DIR/arch/arm64/boot/dtb.img
# Kernel Name and Version
CR_VERSION=V8.0
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
# General init
export ANDROID_MAJOR_VERSION=$CR_ANDROID
export PLATFORM_VERSION=$CR_PLATFORM
export $CR_ARCH
##########################################
# Device specific Variables [SM-G930X]
CR_CONFIG_G930=hero_defconfig
CR_VARIANT_G930=G930X
# Device specific Variables [SM-G935X]
CR_CONFIG_G935=hero2_defconfig
CR_VARIANT_G935=G935X
# Device specific Variables [SM-N935X]
CR_CONFIG_N935=gracer_defconfig
CR_VARIANT_N935=N935X
CR_VARIANT_N930=N930X
# Split Defconfigs
CR_CONFIG_TREBLE=treble_defconfig
CR_CONFIG_ONEUI=oneui_defconfig
CR_CONFIG_8890=exynos8890_defconfig
CR_CONFIG_SPLIT=NULL
CR_CONFIG_CRONOS=cronos_defconfig
# Default Config status
CR_ROOT="0"
CR_SELINUX="1"
CR_HALLIC="0"
CR_BOMB="0"
# Compiler Paths
CR_GCC12=~/Android/Toolchains/aarch64-linux-gnu-12.x/bin/aarch64-linux-gnu-
CR_GCC11=~/Android/Toolchains/aarch64-linux-gnu-11.x/bin/aarch64-linux-gnu-
CR_GCC9=~/Android/Toolchains/aarch64-linux-gnu-9.x/bin/aarch64-linux-gnu-
CR_CLANG=~/Android/Toolchains/clang-r399163/bin
CR_GCC4=~/Android/Toolchains/aarch64-linux-android-4.9/bin/aarch64-linux-android-
CR_LINARO=~/Android/Toolchains/aarch64-linaro-4.9.4-linux-gnu/bin/aarch64-linux-gnu-
#####################################################

# Compiler Selection
BUILD_COMPILER()
{
if [ $CR_COMPILER = "1" ]; then
export CROSS_COMPILE=$CR_GCC4
compile="make"
CR_COMPILER="$CR_GCC4"
fi
if [ $CR_COMPILER = "2" ]; then
export CROSS_COMPILE=$CR_LINARO
compile="make"
CR_COMPILER="$CR_LINARO"
fi
if [ $CR_COMPILER = "3" ]; then
export CROSS_COMPILE=$CR_GCC9
compile="make"
CR_COMPILER="$CR_GCC9"
fi
if [ $CR_COMPILER = "4" ]; then
export CROSS_COMPILE=$CR_GCC12
compile="make"
CR_COMPILER="$CR_GCC12"
fi
if [ $CR_COMPILER = "5" ]; then
export CLANG_PATH=$CR_CLANG
export CROSS_COMPILE=$CR_GCC11
export CLANG_TRIPLE=aarch64-linux-gnu-
compile="make CC=clang ARCH=arm64"
export PATH=${CLANG_PATH}:${PATH}
CR_COMPILER="$CR_CLANG"
fi
}

# Clean-up Function

BUILD_CLEAN()
{
if [ $CR_CLEAN = "y" ]; then
     echo " "
     echo " Cleaning build dir"
     $compile clean && $compile mrproper
     rm -r -f $CR_DTB
     rm -rf $CR_DTS/.*.tmp
     rm -rf $CR_DTS/.*.cmd
     rm -rf $CR_DTS/*.dtb
     rm -rf $CR_DIR/.config
     rm -rf $CR_DTS/exynos8890.dtsi
     rm -rf $CR_OUT/*.img
     rm -rf $CR_OUT/*.zip
fi
if [ $CR_CLEAN = "n" ]; then
     echo " "
     echo " Skip Full cleaning"
     rm -r -f $CR_DTB
     rm -rf $CR_DTS/.*.tmp
     rm -rf $CR_DTS/.*.cmd
     rm -rf $CR_DTS/*.dtb
     rm -rf $CR_DIR/.config
     rm -rf $CR_DIR/.version
     rm -rf $CR_DTS/exynos8890.dtsi
fi
}


# Kernel Name Function

BUILD_IMAGE_NAME()
{
	CR_IMAGE_NAME=$CR_NAME-$CR_VERSION-$CR_VARIANT-$CR_DATE
}

# Config Generation Function

BUILD_GENERATE_CONFIG()
{
  # Only use for devices that are unified with 2 or more configs
  echo "----------------------------------------------"
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
  # Split-config support for devices with unified defconfigs (Universal + device)
  if [ $CR_CONFIG_SPLIT = NULL ]; then
    echo " No split config support! "
  else
    echo " Copy $CR_CONFIG_SPLIT "
    cat $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG_SPLIT >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  # Variant Specific configs (Treble, OneUI ...)
  echo " Copy $CR_CONFIG_VAR "
  cat $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG_VAR >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  # CronosKernel Custom defconfig
  echo " Copy $CR_CONFIG_CRONOS "
  cat $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG_CRONOS >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  # Selinux Never Enforce all targets
  if [ $CR_SELINUX = "1" ]; then
    echo " Building Permissive Kernel"
    echo "CONFIG_ALWAYS_PERMISSIVE=y" >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
    CR_IMAGE_NAME=$CR_IMAGE_NAME-Permissive
  fi
  # Invert HALIC Readout when targeting OneUI Q
  if [ $CR_HALLIC = "1" ]; then
    echo " Inverting HALL_IC Status"
    echo "CONFIG_HALL_EVENT_REVERSE=y" >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  # Legacy modem required when targeting the original Note 7
  if [ $CR_BOMB = "1" ]; then
    echo " Legacy BOMB Edition RIL"
	sed -i -- '/CONFIG_MODEM_PIE_REV/d' $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
    echo "# CONFIG_MODEM_PIE_REV is not set" >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  echo " Set $CR_VARIANT to generated config "
  CR_CONFIG=tmp_defconfig
}

# Kernel information Function
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

# Kernel Compile Function
BUILD_ZIMAGE()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building zImage for $CR_VARIANT"
	export LOCALVERSION=-$CR_IMAGE_NAME
  	cp $CR_DTB_MOUNT $CR_DTS/exynos8890.dtsi
	echo "Make $CR_CONFIG"
	$compile $CR_CONFIG
	echo "Make Kernel with $CR_COMPILER"
	$compile -j$CR_JOBS
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

# Device-Tree compile Function
BUILD_DTB()
{
	echo "----------------------------------------------"
	echo " "
	echo "Checking DTB for $CR_VARIANT"
	# This source does compiles dtbs while doing Image
	if [ ! -e $CR_DTB ]; then
        exit 0;
        echo "DTB Failed to Compile"
        echo " Abort "
	else
        echo "DTB Compiled at $CR_DTB"
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

# Ramdisk Function
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
	if [ ! -e $CR_AIK/image-new.img ]; then
        exit 0;
        echo "Boot Image Failed to pack"
        echo " Abort "
	fi
	# Remove red warning at boot
	echo -n "SEANDROIDENFORCE" » $CR_AIK/image-new.img
	# Copy boot.img to Production folder
	if [ ! -e $CR_PRODUCT ]; then
        mkdir $CR_PRODUCT
	fi
	cp $CR_AIK/image-new.img $CR_PRODUCT/$CR_IMAGE_NAME.img
	# Move boot.img to out dir
	if [ ! -e $CR_OUT ]; then
        mkdir $CR_OUT
	fi
	mv $CR_AIK/image-new.img $CR_OUT/$CR_IMAGE_NAME.img
	du -k "$CR_OUT/$CR_IMAGE_NAME.img" | cut -f1 >sizkT
	sizkT=$(head -n 1 sizkT)
	rm -rf sizkT
	echo " "
	$CR_AIK/cleanup.sh
	# Respect CLEAN build rules
	BUILD_CLEAN
}

# Kernel Target Function
BUILD_VAR(){
	echo " Starting $CR_VARIANT kernel build..."
	if [ $CR_VAR = "1" ]; then
		echo " Building $CR_VARIANT Oneui-Q variant "
		CR_CONFIG_VAR=$CR_CONFIG_ONEUI
		CR_VARIANT=$CR_VARIANT-Q
		CR_DTB_MOUNT=$CR_DTS_ONEUI
		CR_RAMDISK=$CR_RAMDISK_Q
		CR_HALLIC="1"
	fi
	if [ $CR_VAR = "2" ]; then
		echo " Building $CR_VARIANT OneUI-P variant "
		CR_CONFIG_VAR=$CR_CONFIG_ONEUI
		CR_VARIANT=$CR_VARIANT-P
		CR_DTB_MOUNT=$CR_DTS_ONEUI
	fi
	if [ $CR_VAR = "3" ]; then
		echo " Building $CR_VARIANT AOSP-Treble variant "
		CR_CONFIG_VAR=$CR_CONFIG_TREBLE
		CR_VARIANT=$CR_VARIANT-Treble
		CR_DTB_MOUNT=$CR_DTS_TREBLE
		CR_RAMDISK=$CR_RAMDISK_Q
	fi
	if [ $CR_VAR = "4" ]; then
		echo " Building $CR_VARIANT Oneui-Treble variant "
		CR_CONFIG_VAR=$CR_CONFIG_ONEUI
		CR_VARIANT=$CR_VARIANT-TrebleTW
		CR_DTB_MOUNT=$CR_DTS_TREBLE
		CR_RAMDISK=$CR_RAMDISK_Q
	fi
}

# Single Target Build Function
BUILD(){
	if [ "$CR_TARGET" = "1" ]; then
		echo " Galaxy S7 Flat "
		CR_CONFIG_SPLIT=$CR_CONFIG_G930
		CR_DTSFILES=$CR_DTSFILES_G930
		CR_VARIANT=$CR_VARIANT_G930
	fi
	if [ "$CR_TARGET" = "2" ]; then
		echo " Galaxy S7 Edge "
		CR_CONFIG_SPLIT=$CR_CONFIG_G935
		CR_DTSFILES=$CR_DTSFILES_G935
		CR_VARIANT=$CR_VARIANT_G935
	fi
	if [ "$CR_TARGET" = "3" ] || [ "$CR_TARGET" = "4" ] 
	then
		echo " Galaxy Note 7 FE "
		CR_CONFIG_SPLIT=$CR_CONFIG_N935
		CR_DTSFILES=$CR_DTSFILES_N935
		CR_VARIANT=$CR_VARIANT_N935
	if [ "$CR_TARGET" = "4" ]; then
		echo " Building Bomb Edition "
		CR_BOMB="1"
		CR_VARIANT=$CR_VARIANT_N930
	fi
	fi
	CR_CONFIG=$CR_CONFIG_8890
	BUILD_COMPILER
	BUILD_CLEAN
	BUILD_VAR
	BUILD_IMAGE_NAME
	BUILD_GENERATE_CONFIG
	BUILD_ZIMAGE
	BUILD_DTB
	if [ "$CR_MKZIP" = "y" ]; then # Allow Zip Package for mass compile only
	echo " Start Build ZIP Process "
	PACK_KERNEL_ZIP
	else
	PACK_BOOT_IMG
	BUILD_OUT
	fi
}

# Multi-Target Build Function
BUILD_ALL(){
echo "----------------------------------------------"
echo " Compiling ALL targets "
CR_TARGET=1
BUILD
CR_TARGET=2
BUILD
CR_TARGET=3
BUILD
CR_TARGET=4
BUILD
}


# Pack All Images into ZIP
PACK_KERNEL_ZIP() {
echo "----------------------------------------------"
echo " Packing ZIP "

# Variables
CR_BASE_KERNEL=$CR_OUTZIP/floyd/G960F-kernel
CR_BASE_DTB=$CR_OUTZIP/floyd/G960F-dtb

# Check packages
if ! dpkg-query -W -f='${Status}' bsdiff  | grep "ok installed"; then 
	echo " bsdiff is missing, please install with sudo apt install bsdiff" 
	exit 0; 
fi

# Initalize with base image (herolte)
if [ "$CR_TARGET" = "1" ]; then # Always must run ONCE during BUILD_ALL otherwise fail. Setup directories
	echo " "
	echo " Kernel Zip Packager "
	echo " Base Target "
	echo " Clean Out directory "
	echo " "
	rm -rf $CR_OUTZIP
	cp -r $CR_ZIP $CR_OUTZIP
	echo " "
	echo " Copying $CR_BASE_KERNEL "
	echo " Copying $CR_BASE_DTB "
	echo " "
	if [ ! -e $CR_KERNEL ] || [ ! -e $CR_DTB ]; then
        exit 0;
        echo " Kernel not found!"
        echo " Abort "
	else
        cp $CR_KERNEL $CR_BASE_KERNEL
        cp $CR_DTB $CR_BASE_DTB
	fi
	# Set kernel version
fi
if [ ! "$CR_TARGET" = "1" ]; then # Generate patch files for non herolte kernels
	echo " "
	echo " Kernel Zip Packager "
	echo " "
	echo " Generating Patch kernel for $CR_VARIANT "
	echo " "
	if [ ! -e $CR_KERNEL ] || [ ! -e $CR_DTB ]; then
        echo " Kernel not found! "
        echo " Abort "
        exit 0;
	else
		bsdiff $CR_BASE_KERNEL $CR_KERNEL $CR_OUTZIP/floyd/$CR_VARIANT-kernel
		if [ ! -e $CR_OUTZIP/floyd/$CR_VARIANT-kernel ]; then
			echo "ERROR: bsdiff $CR_BASE_KERNEL $CR_KERNEL $CR_OUTZIP/floyd/$CR_VARIANT-kernel Failed!"
			exit 0;
		fi
		bsdiff $CR_BASE_DTB $CR_DTB $CR_OUTZIP/floyd/$CR_VARIANT-dtb
		if [ ! -e $CR_OUTZIP/floyd/$CR_VARIANT-kernel ]; then
			echo "ERROR: bsdiff $CR_BASE_KERNEL $CR_DTB $CR_OUTZIP/floyd/$CR_VARIANT-dtb Failed!"
			exit 0;
		fi
	fi
fi
if [ "$CR_TARGET" = "6" ]; then # Final kernel build
	echo " Generating ZIP Package for $CR_NAME-$CR_VERSION-$CR_DATE"
	sed -i "s/fkv/$zver/g" $CR_OUTZIP/META-INF/com/google/android/update-binary
	cd $CR_OUTZIP && zip -r $CR_PRODUCT/$zver.zip * && cd $CR_DIR
	du -k "$CR_PRODUCT/$zver.zip" | cut -f1 >sizdz
	sizdz=$(head -n 1 sizdz)
	rm -rf sizdz
	echo " "
	echo "----------------------------------------------"
	echo "$CR_NAME kernel build finished."
	echo "Compiled Package Size = $sizdz Kb"
	echo "$CR_NAME-$CR_VERSION-$CR_DATE.zip Ready"
	echo "Press Any key to end the script"
	echo "----------------------------------------------"
fi
}

# Main Menu
clear
echo "----------------------------------------------"
echo "$CR_NAME $CR_VERSION Build Script $CR_DATE"
echo " "
echo " "
echo "1) herolte" "2) hero2lte" "3) gracerlte" "4) gracelte" 
echo  " "
echo "5) Build All/ZIP"               "6) Abort"
echo "----------------------------------------------"
read -p "Please select your build target (1-6) > " CR_TARGET
echo " "
echo "1) OneUI-Q" "2) OneUI-P"
read -p "Please select your build Variant (1-2) > " CR_VAR
echo "----------------------------------------------"
echo " "
echo "1) $CR_GCC4 (GCC 4.9)"
echo "2) $CR_LINARO (Linaro 4.9.4)" 
echo "3) $CR_GCC9 (GCC 9.x)" 
echo "4) $CR_GCC12 (GCC 12.x)" 
echo "5) $CR_CLANG (CLANG)" 
echo " "
read -p "Please select your compiler (1-5) > " CR_COMPILER
echo " "
echo "1) Selinux Permissive " "2) Selinux Enforcing"
echo " "
read -p "Please select your SElinux mode (1-2) > " CR_SELINUX
echo " "
if [ "$CR_TARGET" = "6" ]; then
echo "Build Aborted"
exit
fi
echo " "
read -p "Clean Builds? (y/n) > " CR_CLEAN
echo " "
# Call functions
if [ "$CR_TARGET" = "5" ]; then
echo " "
read -p "Build Flashable ZIP ? (y/n) > " CR_MKZIP
echo " "
BUILD_ALL
else
BUILD
fi

# DEPRECATED FUNCTIONS / UNCOMPLETED

# Functions

# Got Root?
#read -p "Kernel SU? (y/n) > " yn
#if [ "$yn" = "Y" -o "$yn" = "y" ]; then
#     echo " WARNING : KernelSU Enabled!"
#     export CONFIG_ASSISTED_SUPERUSER=y
#     CR_ROOT="1"
#fi

#BUILD_ROOT()
#{
#if [ $CR_ROOT = 1 ]; then
#     echo " "
#     echo " WARNING : KernelSU Enabled!"
#     mv $CR_PRODUCT/$CR_IMAGE_NAME.img $CR_PRODUCT/$CR_IMAGE_NAME-KernelSU.img
#     CR_IMAGE_NAME=$CR_IMAGE_NAME-KernelSU
#fi
#}


#!/bin/bash
set -e
exec 9>.kernelsu-fetch-lock
flock -n 9 || exit 0
[[ $(( $(date +%s) - $(stat -c %Y "drivers/kernelsu/.check" 2>/dev/null || echo 0) )) -gt 86400 ]] || exit 0

AUTHOR="tiann"
REPO="KernelSU"
VERSION=`curl -s -I -k "https://api.github.com/repos/$AUTHOR/$REPO/commits?per_page=1" | sed -n '/^[Ll]ink:/ s/.*"next".*page=\([0-9]*\).*"last".*/\1/p'`

if [[ -f drivers/kernelsu/.version && *$(cat drivers/kernelsu/.version)* == *$VERSION* ]]; then
	touch drivers/kernelsu/.check
	exit 0
fi

# printf "$REPO updating to $((10000+$VERSION+200))\n"
rm -rf drivers/kernelsu
mkdir -p drivers/kernelsu
cd drivers/kernelsu
wget -q -O - https://github.com/$AUTHOR/$REPO/archive/refs/heads/main.tar.gz | tar -xz --strip=2 "$REPO-main/kernel"
echo $VERSION >> .version
touch .check

# You can patch for your kernel here
echo "" >> Makefile
sed -i '/warning /d' Makefile
sed -i '/DKSU_VERSION/d' Makefile
echo "ccflags-y += -DKSU_VERSION=$((10000 + $VERSION + 200))" >> Makefile
gawk -i inplace '/11,/{c++;if(c==2||c==3){sub("11,","9,");}}1' sucompat.c

# 3.18 Kernel Patches

# Fix pointer error
sed -i 's/PTR_ERR(fp));/ (int)PTR_ERR(fp));/' uid_observer.c

# Add uaccess Header
sed -i '/#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)/i #include "linux/uaccess.h"' kernel_compat.c

# Add AIO Header To address kiocb on 3.18
sed -i '/#include "allowlist.h"/i #include "linux/aio.h"' ksud.c

# Use input.h Header instead of input-event-codes.h
sed -i 's/#include "linux\/input-event-codes.h"/#include "linux\/input.h"/' ksud.c

# Remove unsupported cflags
sed -i 's/ccflags-y += -Wno-implicit-function-declaration -Wno-strict-prototypes -Wno-int-conversion -Wno-gcc-compat/ccflags-y += -Wno-implicit-function-declaration -Wno-strict-prototypes/' Makefile

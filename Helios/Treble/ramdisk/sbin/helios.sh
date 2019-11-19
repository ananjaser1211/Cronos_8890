#!/system/bin/sh
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Originally Coded by Tkkg1994 @GrifoDev, BlackMesa @XDAdevelopers
# Reworked by Ananjaser1211 & corsicanu @XDAdevelopers with some code from 6h0st@ghost.com.ro
# resetprop by @nkk71 (R.I.P.), renamed to fakeprop to avoid Magisk conflicts
#

PATH=/sbin:/system/sbin:/system/bin:/system/xbin:/helios
export PATH
RUN=/sbin/busybox;
LOGFILE=/data/helios/boot.log
REBOOTLOGFILE=/data/helios/reboot.log

if [ -e /data/helios ]; then
for FILE in /data/helios/*; do
  $RUN rm -f $FILE
done;
fi

log_print() {
  echo "$1"
  echo "$1" >> $LOGFILE
}
rebootlog_print() {
  echo "$1"
  echo "$1" >> $REBOOTLOGFILE
}

log_print "------------------------------------------------------"
log_print "**helios boot script started at $( date +"%d-%m-%Y %H:%M:%S" )**"

   log_print "Creat Dirs"

if [ ! -e /data/helios ]; then
	mkdir -p /data/helios
	chown -R root.root /data/helios
	chmod -R 755 /data/helios
fi

if [ ! -e /data/heliosLogcat ]; then
  mkdir -p /data/heliosLogcat
  chown -R root.root /data/heliosLogcat
  chmod -R 755 /data/heliosLogcat
fi

   log_print "Backup previous logcat"

if [ -e /data/helios/Refined_logger.log ]; then
  cp "/data/helios/Refined_logger.log" "/data/heliosLogcat/Refined_Logger_$(date +"%d-%m-%Y %H:%M:%S").log"
fi

   log_print "Mounting"

# Initial
mount -o remount,rw -t auto /
mount -t rootfs -o remount,rw rootfs
mount -o remount,rw -t auto /system
mount -o remount,rw /vendor
mount -o remount,rw /data
mount -o remount,rw /cache

# Remount vendor rw
mount -o remount,rw -t auto /vendor

# Create init.d folder if not exist
if [ ! -d /vendor/etc/init.d ]; then
    mkdir -p /vendor/etc/init.d;
fi

chown -R root.root /vendor/etc/init.d;
chmod 777 /vendor/etc/init.d;

if [ "$(ls -A /vendor/etc/init.d)" ]; then
    chmod 777 /vendor/etc/init.d/*;

    for FILE in /vendor/etc/init.d/*; do
        log_print "Trying to execute - $FILE"
        sh $FILE >/dev/null;
        log_print "$FILE executed"
    done;
else
    log_print "No vendor init.d files found"
fi

   log_print "Enforcing"
# Change to Enforce Status.
chmod 644 /sys/fs/selinux/enforce
setenforce 0
# Fix SafetyNet by Repulsa
chmod 640 /sys/fs/selinux/enforce

   log_print "Reset knox flags"

## Custom FLAGS reset
# Tamper fuse prop set to 0 on running system
/sbin/fakeprop -n ro.boot.warranty_bit "0"
/sbin/fakeprop -n ro.warranty_bit "0"
# Fix safetynet flags
/sbin/fakeprop -n ro.boot.veritymode "enforcing"
/sbin/fakeprop -n ro.boot.verifiedbootstate "green"
/sbin/fakeprop -n ro.boot.flash.locked "1"
/sbin/fakeprop -n ro.boot.ddrinfo "00000001"
/sbin/fakeprop -n ro.build.selinux "1"
# Samsung related flags
/sbin/fakeprop -n ro.fmp_config "1"
/sbin/fakeprop -n ro.boot.fmp_config "1"
/sbin/fakeprop -n sys.oem_unlock_allowed "0"

   log_print "Disabling Panics"

# Panic off
$RUN sysctl -w vm.panic_on_oom=0
$RUN sysctl -w kernel.panic_on_oops=0
$RUN sysctl -w kernel.panic=0

   log_print "Removing RMM"

# RMM patch (part)
if [ -d /system/priv-app/Rlc ]; then
	rm -rf /system/priv-app/Rlc
fi

   log_print "Remove SecurityLogAgent"

# Disabling unauthorized changes warnings...
if [ -d /system/app/SecurityLogAgent ]; then
rm -rf /system/app/SecurityLogAgent
fi

   log_print "Add Personalists"

# Write personalists xml for libpersona.so

if [ ! -f /data/system/users/0/personalist.xml ]; then
	touch /data/system/users/0/personalist.xml
fi;
if [ ! -r /data/system/users/0/personalist.xml ]; then
 	chmod 600 /data/system/users/0/personalist.xml
 	chown system:system /data/system/users/0/personalist.xml
fi;

   log_print "Disable Tracing"

# Tweaking logging, debugubg, tracing
dmesg -n 1 -C
$RUN echo "N" > /sys/kernel/debug/debug_enabled
$RUN echo "N" > /sys/kernel/debug/seclog/seclog_debug
$RUN echo "0" > /sys/kernel/debug/tracing/tracing_on

   log_print "ChainFire deepsleep fix"

# Deepsleep fix @Chainfire
for i in `ls /sys/class/scsi_disk/`; do
	cat /sys/class/scsi_disk/$i/write_protect 2>/dev/null | grep 1 >/dev/null
	if [ $? -eq 0 ]; then
		echo 'temporary none' > /sys/class/scsi_disk/$i/cache_type
	fi
done

   log_print "GoogePlayWakelocks fix"

# Google play services wakelock fix
sleep 1
su -c "pm enable com.google.android.gms/.update.SystemUpdateActivity"
su -c "pm enable com.google.android.gms/.update.SystemUpdateService"
su -c "pm enable com.google.android.gms/.update.SystemUpdateService$ActiveReceiver"
su -c "pm enable com.google.android.gms/.update.SystemUpdateService$Receiver"
su -c "pm enable com.google.android.gms/.update.SystemUpdateService$SecretCodeReceiver"
su -c "pm enable com.google.android.gsf/.update.SystemUpdateActivity"
su -c "pm enable com.google.android.gsf/.update.SystemUpdatePanoActivity"
su -c "pm enable com.google.android.gsf/.update.SystemUpdateService"
su -c "pm enable com.google.android.gsf/.update.SystemUpdateService$Receiver"
su -c "pm enable com.google.android.gsf/.update.SystemUpdateService$SecretCodeReceiver"

   log_print "Remount"

mount -o remount,ro -t auto /
mount -t rootfs -o remount,ro rootfs
mount -o remount,ro -t auto /system
mount -o remount,ro /vendor
mount -o remount,rw /data
mount -o remount,rw /cache

   #log_print "Start RefinedLogger"

   log_print "**helios early boot script finished at $( date +"%d-%m-%Y %H:%M:%S" )**"
   log_print "------------------------------------------------------"

   # RefinedLogger
#/system/bin/logcat *:E > /data/helios/Refined_logger.log

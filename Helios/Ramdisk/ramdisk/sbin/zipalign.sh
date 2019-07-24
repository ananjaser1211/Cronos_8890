#!/system/bin/sh
RUN=/sbin/busybox;

# Zipalign /Data
$RUN mount -o rw,seclabel,remount rootfs /
# Needed for log
if [ ! -d /data/helios ]; then
mkdir -p /data/helios
fi
# Begin
chown 0:0 /data/helios/zipalign.log
chmod 755 /data/helios/zipalign.log

LOG_FILE=/data/helios/zipalign.log
ZIPALIGNDB=/data/helios/zipalign.db

if [ -e $LOG_FILE ]; then
	rm $LOG_FILE
fi

if [ ! -f $ZIPALIGNDB ]; then
	touch $ZIPALIGNDB
fi

$RUN echo "# ZipAligner Log" | tee -a $LOG_FILE
$RUN echo "" | tee -a $LOG_FILE
$RUN echo "Starting Zipaligning at: $( date +"%m-%d-%Y %H:%M:%S" )" | tee -a $LOG_FILE

$RUN sleep 10

for DIR in /data/app; do
  cd $DIR
  for APK in *.apk; do
    if [ $APK -ot $ZIPALIGNDB ] && [ $(grep "$DIR/$APK" $ZIPALIGNDB|wc -l) -gt 0 ]; then
      echo "Already checked: $DIR/$APK" | tee -a $LOG_FILE
    else
       /system/xbin/zipalign -c 4 $APK
      if [ $? -eq 0 ]; then
        echo "Already aligned: $DIR/$APK" | tee -a $LOG_FILE
        grep "$DIR/$APK" $ZIPALIGNDB > /dev/null || echo $DIR/$APK >> $ZIPALIGNDB
      else
        echo "Now aligning: $DIR/$APK" | tee -a $LOG_FILE
        /system/xbin/zipalign -f 4 $APK /cache/$APK
        $RUN mount -o rw,remount /system
        cp -f -p /cache/$APK $APK
		chmod 644 $APK
        $RUN rm -f /cache/$APK
        grep "$DIR/$APK" $ZIPALIGNDB > /dev/null || echo $DIR/$APK >> $ZIPALIGNDB
      fi
    fi
  done
done

#Zipalign /system 

for DIR in /system/app; do
  cd $DIR
  for APK in *.apk; do
    if [ $APK -ot $ZIPALIGNDB ] && [ $(grep "$DIR/$APK" $ZIPALIGNDB|wc -l) -gt 0 ]; then
      echo "Already checked: $DIR/$APK" | tee -a $LOG_FILE
    else
      /system/xbin/zipalign -c 4 $APK
      if [ $? -eq 0 ]; then
        echo "Already aligned: $DIR/$APK" | tee -a $LOG_FILE
        grep "$DIR/$APK" $ZIPALIGNDB > /dev/null || echo $DIR/$APK >> $ZIPALIGNDB
      else
        echo "Now aligning: $DIR/$APK" | tee -a $LOG_FILE
        /system/xbin/zipalign -f 4 $APK /cache/$APK
        $RUN mount -o rw,remount /system
        cp -f -p /cache/$APK $APK
		chmod 644 $APK
        $RUN rm -f /cache/$APK
        grep "$DIR/$APK" $ZIPALIGNDB > /dev/null || echo $DIR/$APK >> $ZIPALIGNDB
      fi
    fi
  done
done

for DIR in /system/priv-app; do
  cd $DIR
  for APK in *.apk; do
    if [ $APK -ot $ZIPALIGNDB ] && [ $(grep "$DIR/$APK" $ZIPALIGNDB|wc -l) -gt 0 ] ; then
      echo "Already checked: $DIR/$APK" | tee -a $LOG_FILE
    else
      /system/xbin/zipalign -c 4 $APK
      if [ $? -eq 0 ]; then
        echo "Already aligned: $DIR/$APK" | tee -a $LOG_FILE
        grep "$DIR/$APK" $ZIPALIGNDB > /dev/null || echo $DIR/$APK >> $ZIPALIGNDB
      else
        echo "Now aligning: $DIR/$APK" | tee -a $LOG_FILE
        /system/xbin/zipalign -f 4 $APK /cache/$APK
        $RUN mount -o rw,remount /system
        cp -f -p /cache/$APK $APK
		chmod 644 $APK
        $RUN rm -f /cache/$APK
        grep "$DIR/$APK" $ZIPALIGNDB > /dev/null || echo $DIR/$APK >> $ZIPALIGNDB
      fi
    fi
  done
done

for DIR in /system/framework; do
  cd $DIR
  for APK in *.apk ; do
    if [ $APK -ot $ZIPALIGNDB ] && [ $(grep "$DIR/$APK" $ZIPALIGNDB|wc -l) -gt 0 ]; then
      echo "Already checked: $DIR/$APK" | tee -a $LOG_FILE
    else
      /system/xbin/zipalign -c 4 $APK
      if [ $? -eq 0 ]; then
        echo "Already aligned: $DIR/$APK" | tee -a $LOG_FILE
        grep "$DIR/$APK" $ZIPALIGNDB > /dev/null || echo $DIR/$APK >> $ZIPALIGNDB
      else
        echo "Now aligning: $DIR/$APK" | tee -a $LOG_FILE
        /system/xbin/zipalign -f 4 $APK /cache/$APK
        $RUN mount -o rw,remount /system
        cp -f -p /cache/$APK $APK
		chmod 644 $APK
        $RUN rm -f /cache/$APK
        grep "$DIR/$APK" $ZIPALIGNDB > /dev/null || echo $DIR/$APK >> $ZIPALIGNDB
      fi
    fi
  done
done

touch $ZIPALIGNDB
$RUN echo "Zipaligning finished at $( date +"%m-%d-%Y %H:%M:%S" )" | tee -a $LOG_FILE

chown 0:0 /data/helios/zipalign.log
chmod 755 /data/helios/zipalign.log

mount -o ro,seclabel,remount rootfs /

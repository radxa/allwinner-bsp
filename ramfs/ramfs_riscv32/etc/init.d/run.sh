#!/bin/sh
export hostname0=
export mnt_path0=
export hostname1=
export mnt_path1=
export auto=
dmesg > dmesg_boot.log
echo $(grep Machine /dmesg_boot.log| sed 's/^.*Andes //g') > platform.log
linux_ver=`uname -r`

if [ -d /lib/modules/$linux_ver/ ]; then
        while read module
        do
                if [ "$auto" == "1" ]; then
                        module_name=`basename $module .ko`
                        rmmod $module_name
                fi
                insmod /lib/modules/$linux_ver/$module

        done</lib/modules/$linux_ver/modules.order
fi

#change SHELL default version
#if [ -f /bin/bash ]; then
#        ln -sf /bin/bash /bin/sh
#fi

if [ "$auto" != "1" ]; then
        exit 1
fi

#for i in $drvs
#do
#drv=$(dmesg | grep "$i")
#if [ "$?" == "1" ]; then
#	echo "$i NOT exist"
#	exit
#fi
#done

#exit

test -z $hostname1
if [ "$?" == "1" ]; then
	echo "mount hastname1 = $hostname1"
	mount -t nfs -o nolock,rsize=1024,wsize=1024 $hostname1:$mnt_path1 mnt
else
	echo "mount hastname0 = $hostname0"
	mount -t nfs -o nolock,rsize=1024,wsize=1024 $hostname0:$mnt_path0 mnt
fi

if [ "$?" != "0" ]; then
	echo "mount mnt fail"
	exit 1
fi
lsmod 2>&1 | tee mnt/log/lsmod.log
cp dmesg_boot.log mnt/log/
mount /dev/mmcblk0p1 /tmp
if [ "$?" != "0" ]; then
	echo "mount tmp fail"
	exit 1
fi

cd
cd mnt
dmesg > log/dmesg_run.log
sh test_drivers.sh

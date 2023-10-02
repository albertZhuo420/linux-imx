#!/bin/env bash

function print_usage()
{
	echo "***************Usage Start ***************"
	echo "	$0 b: Use busybox"
	echo "		or"
	echo "	$0 s: Use systemd"
	echo "***************Usage End *****************"
}

BUSYBOX_FS_PATH="/home/zhuo/work/server-dir/nfs/rootfs/buildroot-2023.05.1/busybox/usr"
SYSTEMD_FS_PATH="/home/zhuo/work/server-dir/nfs/rootfs/buildroot/systemd/arm/imx/usr"

if [ $# == 0 ]; then
	print_usage;
	exit 1
else
	if [ $1 == 'b' ]; then 
		if [ -d $BUSYBOX_FS_PATH ]; then
			make modules_install INSTALL_MOD_PATH=$BUSYBOX_FS_PATH
		else 
			echo "BUSYBOX Dir path does not exits."
			exit 2
		fi
		# echo $BUSYBOX_FS_PATH
	elif [ $1 == 's' ]; then
		if [ -d $SYSTEMD_FS_PATH ]; then
			make modules_install INSTALL_MOD_PATH=$SYSTEMD_FS_PATH
		else
			echo "SYSTEMD Dir path does not exits."
			exit 3
		fi
		# echo $SYSTEMD_FS_PATH
	else
		print_usage
		exit 4
	fi
fi

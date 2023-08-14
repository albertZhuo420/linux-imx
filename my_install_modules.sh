#!/bin/env bash

function print_usage()
{
	echo "***************Usage Start ***************"
	echo "	./shell.sh b: Use busybox"
	echo "		or"
	echo "	./shell.sh s: Use systemd"
	echo "***************Usage End *****************"
}

BUSYBOX_FS_PATH="/home/zhuo/work/server-dir/nfs/rootfs/buildroot-2023.05.1/busybox/usr"
SYSTEMD_FS_PATH="/home/zhuo/work/server-dir/nfs/rootfs/buildroot-2023.05.1/systemd/usr"

if [ $# == 0 ]; then
	print_usage;
	exit 1
else
	if [ $1 == 'b' ]; then 
		make modules_install INSTALL_MOD_PATH=$BUSYBOX_FS_PATH
		# echo $BUSYBOX_FS_PATH
	elif [ $1 == 's' ]; then
		make modules_install INSTALL_MOD_PATH=$SYSTEMD_FS_PATH
		# echo $SYSTEMD_FS_PATH
	else
		print_usage
		exit 2
	fi
fi

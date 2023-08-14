#!/bin/env bash

function print_usage()
{
	echo "***************Usage Start ***************"
	echo "	source shell.sh b: Use busybox sdk"
	echo "		or"
	echo "	source shell.sh s: Use systemd sdk"
	echo "***************Usage End *****************"
}

SDK_PATH="/home/zhuo/work/cross-toolchain/setup_scripts/buildroot-20230501"
BUSYBOX_SDK_PATH="$SDK_PATH/busybox-sdk.sh"
SYSTEMD_SDK_PATH="$SDK_PATH/systemd-sdk.sh"

if [ $# == 0 ]; then
	print_usage;
	# exit 1 # 使用 source 就不能直接用 exit 退出了, 否则会推出整个shell
else
	if [ $1 == 'b' ]; then 
		. $BUSYBOX_SDK_PATH
		# echo $BUSYBOX_SDK_PATH
	elif [ $1 == 's' ]; then
		. $SYSTEMD_SDK_PATH
		# echo $SYSTEMD_SDK_PATH
	else
		print_usage
		# exit 2
	fi
fi

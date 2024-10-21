#!/bin/bash
#
# Copyright (C) 2023 Corellium LLC
#
# Author: Ernesto A. Fernández <ernesto@corellium.com>
#
# Call as ./test.sh to test mkfs on various container sizes. Some will be very
# big so this should always be done inside a filesystem that supports sparse
# files.

success=0

set -e
cleanup() {
	rm -f /tmp/sizetest.img
	rm -f /tmp/sizetest2.img
	[ $success -eq 1 ] && echo "TEST PASSED" || echo "TEST FAILED"
}
trap cleanup exit

test_size() {
	truncate -s $1 /tmp/sizetest.img
	./mkapfs /tmp/sizetest.img
	../apfsck/apfsck -cuw /tmp/sizetest.img
}

test_fusion_sizes() {
	truncate -s $1 /tmp/sizetest.img
	truncate -s $2 /tmp/sizetest2.img
	./mkapfs -F /tmp/sizetest2.img /tmp/sizetest.img
	../apfsck/apfsck -cuw -F /tmp/sizetest2.img /tmp/sizetest.img
}

confirm_mkfs_failure() {
	truncate -s $1 /tmp/sizetest.img
	truncate -s $2 /tmp/sizetest2.img
	# Confirm that this fails cleanly, not with sigbus
	./mkapfs -F /tmp/sizetest2.img /tmp/sizetest.img >/dev/null 2>&1 || [ $? -eq 1 ]
}

# Single block ip bitmap, single block spaceman, no CABs
sizes[0]=512K # Minimum size
sizes[1]=15G
sizes[2]=1454383300608	# Maximum size

# Multiblock ip bitmap, single block spaceman, no CABs
sizes[3]=1454383304704	# Minimum size
sizes[4]=3T
sizes[5]=7390296539136	# Maximum size

# Multiblock ip bitmap, multiblock spaceman, no CABs
sizes[6]=7390296543232	# Minimum size
sizes[7]=7T
sizes[8]=8574096900096	# Maximum size

# Multiblock ip bitmap, single block spaceman, has CABs
sizes[9]=8574096904192	# Minimum size
sizes[10]=15T

# Filesystems > ~113 TiB not yet supported

# Regression tests for sizes that caused problems in the past
sizes[11]=3G

touch /tmp/sizetest.img
for sz in ${sizes[@]}; do
	test_size $sz
done

touch /tmp/sizetest2.img
for sz1 in ${sizes[@]}; do
	for sz2 in ${sizes[@]}; do
		# The main device needs to be large enough to map tier 2
		if [ "$sz1" = "512K" -a "$sz2" != "512K" ]; then
			confirm_mkfs_failure $sz1 $sz2
		else
			test_fusion_sizes $sz1 $sz2
		fi
	done
done

success=1

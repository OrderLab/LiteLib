#!/bin/bash

case $CRTOOLS_SCRIPT_ACTION in
	(pre-dump)
		echo "pre-dump time `date +%s%N`"
		;;
	(post-dump)
		rm -rf foobak/
		rsync -a foo/ foobak/
		echo "post-dump time `date +%s%N`"
		;;
	(pre-restore)
    # rm -rf /foo
		echo "pre-restore time `date +%s%N`"
		mv foo/ foo_before_restore/
		mv foobak/ foo
		;;
	(post-resume)
		echo "boot time `date +%s%N`" >> foo_before_restore/foo/reboot_time.log
		echo "post-resume time `date +%s%N`"
		;;
	(*) echo "No action for " $CRTOOLS_SCRIPT_ACTION;;
esac
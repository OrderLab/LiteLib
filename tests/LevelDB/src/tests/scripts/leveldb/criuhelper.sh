#!/bin/bash

case $CRTOOLS_SCRIPT_ACTION in
	(post-dump)
		rm -rf foobak/
		rsync -a foo/ foobak/
		;;
	(pre-restore)
    # rm -rf /foo
		mv foo/ foo_before_restore/
		mv foobak/ foo
		;;
	(post-resume)
		echo "boot time `date +%s%N`" >> foo_before_restore/foo/reboot_time.log
		;;
	(*) echo "No action for " $CRTOOLS_SCRIPT_ACTION;;
esac
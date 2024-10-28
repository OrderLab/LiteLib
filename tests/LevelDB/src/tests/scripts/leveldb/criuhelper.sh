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
	(*) echo "No action for " $CRTOOLS_SCRIPT_ACTION;;
esac
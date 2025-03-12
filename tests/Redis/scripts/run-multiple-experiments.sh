#!/bin/bash

set -x

SCRIPT_DIR=$(realpath "$(dirname "$0")")

# for SUFFIX in {1..10}; do
#     for MODE in vanilla embedded replica; do
#         ./run-single-experiment.sh $MODE $SUFFIX 0 > $SCRIPT_DIR/logs/$MODE-experiment-script-$SUFFIX.log 2>&1
#     done
# done

SUFFIX=recovery
for MODE in vanilla replica embedded; do
    ./run-single-experiment.sh $MODE $SUFFIX 1 > $SCRIPT_DIR/logs/$MODE-experiment-script-$SUFFIX.log 2>&1
done
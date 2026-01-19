#!/bin/bash

set -x

SCRIPT_DIR=$(realpath "$(dirname "$0")")

mkdir -p $SCRIPT_DIR/logs

# SUFFIX=5000ms-down-after-milliseconds
# for i in {4..6}; do
#     for MODE in replica read-only-replica; do
#         ./run-single-experiment.sh $MODE $SUFFIX-$i 1 > $SCRIPT_DIR/logs/$MODE-experiment-script-$SUFFIX-$i.log 2>&1
#     done
# done

# SUFFIX=recovery
# for i in {1..3}; do
#     for MODE in vanilla replica read-only-replica embedded; do
#         ./run-single-experiment.sh $MODE $SUFFIX-$i 1 > $SCRIPT_DIR/logs/$MODE-experiment-script-$SUFFIX-$i.log 2>&1
#     done
# done

SUFFIX=overhead
for i in {1..20}; do
    for MODE in vanilla replica read-only-replica embedded; do
        ./run-single-experiment.sh $MODE $SUFFIX-$i 0 > $SCRIPT_DIR/logs/$MODE-experiment-script-$SUFFIX-$i.log 2>&1
    done
done

# for SUFFIX in {1..10}; do
#     for MODE in vanilla embedded replica; do
#         ./run-single-experiment.sh $MODE $SUFFIX 0 > $SCRIPT_DIR/logs/$MODE-experiment-script-$SUFFIX.log 2>&1
#     done
# done

# SUFFIX=recovery
# MODE=embedded
# for i in {3..3}; do
#     ./run-single-experiment.sh $MODE $SUFFIX-$i 1 > $SCRIPT_DIR/logs/$MODE-experiment-script-$SUFFIX-$i.log 2>&1
# done

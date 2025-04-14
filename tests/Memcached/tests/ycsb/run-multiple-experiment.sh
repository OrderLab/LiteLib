#!/bin/bash

set -x

Dir=$(dirname $0)

for i in {10..10}; do
  for j in vanilla proxy embedded; do
    $Dir/run-single-experiment.sh $j $i
    sleep 10
  done
done


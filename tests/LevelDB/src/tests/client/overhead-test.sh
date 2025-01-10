#!/bin/bash

for i in {10000..35000..5000}; do
  for type in lite full; do
      echo "Running $type experiment with $i rps..."
      cp "env.yaml.real.$type" env.yaml
      sed -i "s/rps: .*$/rps: $i/g" env.yaml
      sed -i "s/file_prefix: .*$/file_prefix: $type-$i/g" env.yaml
      cargo run --release
      sleep 30
  done
done

# for i in $(seq 0 0.1 0.5); do
#   for type in lite full; do
#       echo "Running $type experiment with $i write ratio..."
#       cp "env.yaml.real.$type" env.yaml
#       sed -i "s/write_ratio: .*$/write_ratio: $i/g" env.yaml
#       sed -i "s/file_prefix: .*$/file_prefix: $type-$i/g" env.yaml
#       cargo run --release
#       sleep 30
#   done
# done

# for i in $(seq 0 0.1 0.5); do
#   for type in lite full; do
#     python3 ../scripts/client/plot_preprocess.py -f $type-$i.jsonl
#     python3 ../scripts/client/plot.py -f $type-$i.stat.json > $type-$i.process.txt
#   done
# done


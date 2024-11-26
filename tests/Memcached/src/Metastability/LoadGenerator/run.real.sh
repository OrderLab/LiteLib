
for type in full lite
do
    echo "Running experiment with type: $type"
    python3 run_experiment.py 30000 0 300 10 256 60 False 1 0 $type
done

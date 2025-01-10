
for type in full lite checkpoint
do
    echo "Running experiment with type: $type"
    python3 run_experiment.py 400 0 300 1.00001 256 60 False 0 $type
done

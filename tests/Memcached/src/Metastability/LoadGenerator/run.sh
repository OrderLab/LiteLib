for load in $(seq 15000 15000 150000)
do
    echo "Running experiment with load: $load"
    echo "Full"
    python3 run_experiment.py $load 0 300 1.00001 12 60 False 1 full
    echo "Lite"
    python3 run_experiment.py $load 0 300 1.00001 12 60 False 1 lite
done
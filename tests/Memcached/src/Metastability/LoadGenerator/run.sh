for ratio in $(seq 0.001 0.001 0.05)
do
    echo "Running experiment with write ratio: $ratio"
    echo "Full"
    python3 run_experiment.py 30000 0 300 1.5 12 60 False 1 $ratio full
    echo "Lite"
    python3 run_experiment.py 30000 0 300 1.5 12 60 False 1 $ratio lite
done

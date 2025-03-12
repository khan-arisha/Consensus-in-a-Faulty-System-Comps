#!/bin/bash

RUNS=20
C_PROGRAM="hotstuff_regular_leader_split.c"
NUM_PROCESSES=9
LOG_FILE="performance_log.txt"

if [ ! -f "$LOG_FILE" ]; then
    echo "Starting Performance Tests" > "$LOG_FILE"
fi

for ((i=1; i<=RUNS; i++))
do
    echo "Run $i/$RUNS - $(date)" | tee -a "$LOG_FILE"
    
    # Runs performance_log
    ./performance "$C_PROGRAM" "$NUM_PROCESSES"
    
    echo "-----------------------------------" >> "$LOG_FILE"
    
    # Sleeps for a few seconds between runs
    sleep 2  
done

echo "All runs completed." | tee -a "$LOG_FILE"


# INSTRUCTIONS
# Step 1: Change the number in runs to run performace_log.c multiple times
# Step 2: Change C_Program to the name of the .c file you want to measure preformace for
#               Note: make sure you already compiled the .c file you want to measure first
# Step 3: Change the number of process to however much you want (max 9)
# Step 4: In terminal write: chmod +x multiple_runs.sh
#               This gives the file execution permissions
# Step 5: In terminal run: ./multiple_runs.sh

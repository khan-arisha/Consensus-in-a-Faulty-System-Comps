# Consensus-in-a-Faulty-System:
2024-25 Carleton Comprehensive Integrative Exercise repository for Consensus in a Faulty System group. The members of the group were Hanane Akeel, Vanessa Heynes, Colin James, Arisha Khan, Luha Yang, and Wesley Yang. Our comprehensive integrative exercise consisted of implementing three different consensus algorithms and comparing the benefits and drawbacks of each. Additionally, we made proofs of k-resilience, the consensus algorithm equivalent to proof of correctness, of each algorithm.


## Description of algorithms and files

### Leaderless Byzantine Paxos:
This consensus algorithm is a variation of Paxos that operates without a designated leader while effectively handling Byzantine faults. This approach enhances fault tolerance and decentralization, ensuring consensus even in the presence of malicious or faulty nodes.

### Raft
Raft is a leader based algorithm that implements leader elections and a heartbeat in order to achieve consensus. It is designed for simplicity and understandability while ensuring strong consistency in distributed systems. This algorithm only accounts for network failure.

### Hotstuff
Hotstuff is our final consensus algorithm. It is a Byzantine Fault Tolerant (BFT) consensus protocol designed for blockchain and distributed systems. It improves upon classical BFT algorithms by using a linear communication complexity and a leader-based pipelining approach to achieve fast and efficient consensus. HotStuff reduces latency and enhances scalability by minimizing the number of message rounds required for finalizing a block.

### Hotstuff nodes
The Hotstuff nodes file works in conjunction with the main Hotstuff file. Use this file to set the number of active nodes and randomly assigned byzantine faults in Hotstuff.

### Hotstuff NF
This file implements network failures for our hotstuff implementation.

## Installation Prerequisites
  - [Open MPI](https://www.open-mpi.org) should be installed on each server in the distributed system to use. We used a cluster of 10 Raspberry Pi.



## Connect to configured ssh host
   - We used <mantis.mathcs.carleton.edu> as the host.
   - Navigate to the directory that contains the consensus algorithms


## Set up ssh to access without passwords:
  - Login to grape00, which will be the main node to run scripts
  - Make ssh-keygen key while logged into grape00


   ssh-keygen


   - Inside the .ssh directory, check file id_rsa.pub to make sure that ‘root=grape00’ is written at the end of the script.
   - Move the id_rsa.pub file into the authorized_keys directory
    
   cat id_rsa.pub >> authorized_keys


  - Login to all other grapes and logout back to grape00.


   ssh grape01
   logout
   ssh grape02
   logout
   ...


   - Repeat the process until you have logged into every Raspberry Pi.
   - No need to specify the grape's entire host name (grape01.mathcs….) because the grapes all know each other.




   ## Compile and run code:
   - compile code while logged into grape00
       - mpicc **<name_of_file.c> -o <name_of_file>**
       - example → **mpicc hello.c -o hello**
   - run code while logged into grape00
   - mpirun -np **<number_of_processes> --host <grape##,grape##> ./<name_of_file>**
   - example → **mpirun -np 2 --host grape00,grape01 ./hello**
   -  ghp_DTX3ZX0rTXVVSqeyaViU5SNI96EaPC4EO0


# How to run Leaderless Byzantine Paxos Implementation
- Log onto the distributed system you are using (In our case it was the grapes)
- On the head node of your choosing, compile the code using the command:
    - mpicc leaderless_paxos.c -o leaderless_paxos
- Then run the compiled code with the command:
    - mpirun -np <insert number of nodes> – host <Insert name of all nodes in your distributed system e.g. grape00,grape01> ./leaderless_paxos
- Summary output should be displayed in the terminal




# How to run Raft Implementation
- mpicc raft.c -o raft
- mpirun -np <number of nodes> —-host <Number of nodes in your distributed system e.g grape00,grape01> ./raft 

# How to run Hotstuff Implementation
- Make sure you are connected to a host and log into your distributed system with open_mpi downloaded
- Log into the head node of your choosing and compile with the command:
    - mpicc hotstuff.c -o hostuff
- Run the code using the command:
    - mpirun -np <number of nodes> —-host <Insert name of all nodes in your distributed system e.g. grape00,grape01> ./hotstuff


# How to run Hotstuff_nodes.py
- Make sure you are connected to a host and log into your distributed system with open_mpi downloaded
- Run python3 hotstuff_nodes.py

# How to run hotstuff_split_NF.c
- Make sure you are connected to a host and log into your distributed system with open_mpi downloaded
- Log into the head node of your choosing and compile with the command:
    - mpicc hotstuff_split_NF.c -o hotstuff_split_NF
- Run the code using the command:
    - mpirun -np <number of nodes> —-host <Insert name of all nodes in your distributed system e.g. grape00,grape01> ./hotstuff_split_N

# How to Collect Data on Algorithms for Visualization:
* make sure to compile performance.c, txt_to_csv.c, and multiple_runs.sh


   ## To compile performance.c:
   - write into terminal: **gcc performance.c -o performance**
   - to run the file: **./performance <MPI_C_Program.c> <num_processes>**
   - EX: ./performance hotstuff_regular.c 3
   - you'll get a message in the terminal that says **"Performance logged to performance_log.txt"**
   - you can then open performance_log.txt to look at the output of the algorithms


   ## To compile txt_to_csv.c:
   - write into terminal: **gcc txt_to_csv.c -o txt_to_csv**
   - to run the file: **./txt_to_csv**
   - you will get an output in the terminal that says **"Conversion completed successfully."**
   - you will get a file made called **performance.csv** that you can then open to look at your data


   ## To compile multiple_runs.sh:
   - Change the number in runs to run performace_log.c however many times you want
   - Change C_Program to the name of the .c file you want to measure performance for
       - Note: make sure you already compiled the .c file you want to measure first
   - Change the number of process to however much you want (max 9)
   - In terminal write: **chmod +x multiple_runs.sh**
       - This gives the file execution permissions
   - In terminal run: **./multiple_runs.sh**


   ## To download the csv files:
   - write in the location of your csv file: **scp [YOUR_NAME]@RasberryPi00.YOUR_EMAIL.org:~/Comps/performance.csv performance.csv**
   - then when you are logged into your local computer write:  **scp [YOUR_NAME]@RasberryPi00.YOUR_EMAIL.org:~/Comps/performance.csv performance.csv**


   ## How I made my data:
   - for the first csv file, I measured each algorithm 100 times at 10 nodes
       - I then saved the csv file
   - for the second csv file, I measured each algorithm 100 times starting at 3 nudes all the way up to 10 nodes
       - I then saved the csv file as performance_10_nodes










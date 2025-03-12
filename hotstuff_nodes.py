import csv
import random
import sys

MAX_NODES = 10
MIN_NODES = 0

def checkNode(nodes):
    if NODES > MAX_NODES:
        print("Error: Number of nodes exceed max nodes")
        sys.exit(1)

    if NODES < MIN_NODES:
        print("Error: Number of nodes needs to be greater than or equal to ", MIN_NODES)
        sys.exit(1)

def checkFaults(nodes):
    if BYZANTINE_FAULTS > NODES:
        print("Error: Number of Byzantine faults exceed number of nodes")
        sys.exit(1)

    if BYZANTINE_FAULTS < MIN_NODES:
        print("Error: Number of faulty nodes need to be equal or greater than 0")
        sys.exit(1)


NODES = int(input("Enter number of nodes: "))
checkNode(NODES)

BYZANTINE_FAULTS = int(input("Enter number of faulty nodes: "))
checkFaults(BYZANTINE_FAULTS)



#assign random nodes faulty
faults = random.sample(range(NODES), BYZANTINE_FAULTS)

# Create a list of nodes
nodes = list(range(NODES))
print("Nodes:", nodes)
print("Faulty nodes:", faults)

# Write to CSV
with open('data.csv', 'w') as file:
    writer = csv.writer(file)
    #header
    writer.writerow(['nodes', 'faulty nodes'])
    #data
    writer.writerow([str(nodes), str(faults)])

# print("CSV file 'data.csv' created successfully.")

#python3 hotstuff_nodes.py
/*
 * Leaderless Byzantine Paxos Implementation
 */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_NODES 7         // Total number of nodes
#define BYZANTINE_FAULTS 3  // Number of Byzantine nodes allowed (up to 3)

// Simulate Byzantine behavior: 0 is Byzantine, 1 is honest
int is_byzantine_node(int rank) {

    // First BYZANTINE_FAULTS nodes are Byzantine
    return (rank < BYZANTINE_FAULTS); 
}

// Generate a "vote" from the node: Byzantine nodes send random values, honest nodes send consistent values
int generate_vote(int rank) {
    if (is_byzantine_node(rank)) {
        int random_num = rand() % 2; // Random vote (0 or 1) for Byzantine nodes
        int result = random_num;
        return result;
    }
    return 1; // Consistent vote for honest nodes
}

// Run the consensus process on each node
int run_bft_consensus(int rank, int world_size) {
    int my_vote = generate_vote(rank);
    int received_votes[NUM_NODES];
    int vote_count[2] = {0};  // To count votes for 0 and 1

    double start, end, latency;
    double total_latency;

    start = MPI_Wtime();
    // Broadcast each node's vote to every other node
    for (int i = 0; i < world_size; i++) {
        if (i != rank) {
            MPI_Send(&my_vote, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
        }
    }

    // Receive votes from all other nodes
    for (int i = 0; i < world_size; i++) {
        if (i != rank) {

            MPI_Recv(&received_votes[i], 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            vote_count[received_votes[i]]++;
            // printf("%d", received_votes[i]);
        } else {
            received_votes[i] = my_vote;
            vote_count[my_vote]++;
        }
    }
    end = MPI_Wtime();
    latency = end - start;
    
    MPI_Reduce(&latency, &total_latency, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    printf("Node %d latency: %f seconds\n", rank, latency);
    if (rank == 0){
        printf("latency: %f\n", latency);
        printf("total latency: %f\n", total_latency);
    }

    // Apply majority rule: Consensus decision based on the majority of votes
    int consensus_decision;
    int qourum = NUM_NODES - ((NUM_NODES-1)/3);

    if (vote_count[1] >= qourum) {
        consensus_decision = 1;
    } else {
        consensus_decision = 0;
    }
    

    int original_vote = my_vote;
    if(my_vote != consensus_decision){
       my_vote = consensus_decision;
    }

    printf("Node %d sees majority vote as %d, old vote: %d new vote %d\n", rank, consensus_decision, original_vote, my_vote);
    return consensus_decision;
}

// Summary function to find and print summary of all nodes
int print_summary(int world_size, int consensus_dec) {
    int faulty_nodes = BYZANTINE_FAULTS;
    int correct_nodes = world_size - faulty_nodes;
    int qourum = world_size - ((world_size -1)/3);

    printf("There were %d total nodes in this round of consensus\n", world_size);
    printf("There were %d faulty nodes that had a value of 0\n", faulty_nodes);
    printf("There were %d correct nodes that had a value of 1\n", correct_nodes);
    printf("Since there was over the qourum size of %d, there was the consensus decision of %d\n", qourum, consensus_dec);

    if (consensus_dec == 1){
        printf("Consensus was reached and the non-lying nodes decided 1\n");
    } else {
        printf("Consensus was not reached and the lying nodes decided 0\n");
    }

    printf("\n=========================\n \n");

    return 0;
}

int main(int argc, char** argv) {
    MPI_Init(NULL, NULL);
    int world_rank, world_size;
    double start_speed, end_speed;
    double speed_time;
    
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (world_size != NUM_NODES) {
        if (world_rank == 0) {
            fprintf(stderr, "This program requires exactly %d nodes.\n", NUM_NODES);
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    // Initialize random seed with rank for unique randomness per node
    srand(time(NULL) + world_rank); 

    start_speed = MPI_Wtime();

    // Run the BFT consensus algorithm
    int consensus = run_bft_consensus(world_rank, world_size);
    end_speed = MPI_Wtime();
    speed_time = end_speed - start_speed;

    if (world_rank == 0){
        printf("speed %f\n", speed_time);
    }

    print_summary(world_size, consensus);

    // Finalize MPI environment
    MPI_Finalize();
    return EXIT_SUCCESS;
}

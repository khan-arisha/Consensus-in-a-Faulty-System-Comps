/*
 * Final version of raft with speed measurement added
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <mpi.h>

/* ------Global variables----- */

// Roles
#define LEADER 0
#define FOLLOWER 1
#define CANDIDATE 2

// Tags
#define VOTE_REQUEST_TAG 1
#define VOTE_RESPONSE_TAG 2
#define HEARTBEAT_TAG 3
#define VALUE_TAG 4
#define ACK_TAG 5

// Timeout and probability
#define HEARTBEAT_TIMEOUT 3.0 // Base timeout in seconds
#define ELECTION_TIMEOUT_MIN 0.15 // Min election timeout
#define ELECTION_TIMEOUT_MAX 0.5 // Max election timeout. Originally set to 0.3
#define LEADER_FAILURE_PROBABILITY 0.1 // Probability of leader failing to send heartbeat.
#define FOLLOWER_FAILURE_PROBABILITY 0.1 // Probability of follower failing to acknowledge proposal

/* ------Structs----- */

typedef struct {
    int proposalMade;
    int decisionMade;
    int value;
} Proposal;

// Raft state structure
typedef struct {
    int me; // Node ID
    int state; // LEADER, FOLLOWER or CANDIDATE
    int currentTerm;

    int votedFor;
    int voteCount;
    int received;

    double electionTimeout;   // Randomized timeout for starting election
    double timeout;

    bool heartbeatReceived;
    double lastHeartbeatTime; // Last time the heartbeat was received (in seconds)

    bool proposed;
    int ackCount;
    int ackReceived;

    double leaderStartTime;
    
    int proposalMade;
    int decisionMade;
    int lastCommittedValue;
} Raft;

void initializeRaft(Raft *rf, int nodeId, int numNodes) {
    rf->me = nodeId;
    rf->currentTerm = 0;
    rf->heartbeatReceived = false;
    rf->votedFor = -1;
    rf->voteCount = 0;
    rf->received = 0;
    rf->lastHeartbeatTime = MPI_Wtime();
    rf->timeout = 0;

    rf->proposed = false;
    rf->ackCount = 0;
    rf->ackReceived = 0;

    rf->leaderStartTime = 0.0;

    rf->proposalMade = 0;
    rf->decisionMade = 0;
    rf->lastCommittedValue = 0;

    srand(time(NULL) + nodeId); // Seed random number generator uniquely per node
    rf->electionTimeout = ELECTION_TIMEOUT_MIN + ((double)rand() / RAND_MAX) * (ELECTION_TIMEOUT_MAX - ELECTION_TIMEOUT_MIN) + HEARTBEAT_TIMEOUT;

    if (nodeId == 0) {
        rf->state = LEADER;
        printf("Node %d is initialized as the LEADER with election timeout: %.2f seconds\n", nodeId, rf->electionTimeout);
    } else {
        rf->state = FOLLOWER;
        printf("Node %d is initialized as a FOLLOWER with election timeout: %.2f seconds\n", nodeId, rf->electionTimeout);
    }
}

/* ------Utility functions----- */

// Start an election
void startElection(Raft *rf, int numNodes) {
    rf->state = CANDIDATE;
    rf->currentTerm++;   // Increment term when starting an election
    rf->voteCount = 1;     // Vote for itself
    rf->received = 1;
    rf->votedFor = rf->me; // Record that it voted for itself

    // Reset election timeout
    rf->electionTimeout = ELECTION_TIMEOUT_MIN + ((double)rand() / RAND_MAX) * (ELECTION_TIMEOUT_MAX - ELECTION_TIMEOUT_MIN) + HEARTBEAT_TIMEOUT;

    printf("Node %d (Term %d) is starting an election\n", rf->me, rf->currentTerm);
    
    // Send vote request to all other nodes
    for (int i = 0; i < numNodes; ++i) {
        if (i != rf->me) {
            MPI_Send(&rf->currentTerm, 1, MPI_INT, i, VOTE_REQUEST_TAG, MPI_COMM_WORLD);
        }
    }

    rf->timeout = MPI_Wtime() + rf->electionTimeout; // Reset timeout after election
}

// Handle vote requests and responses
void handleVoteRequest(Raft *rf, int term, int senderId) {
    // If the term is greater than the current term, update the term and vote
    if (term > rf->currentTerm) {
        rf->votedFor = senderId; // Vote for the candidate
        rf->state = FOLLOWER;    // Become a follower
        printf("Node %d updated to term %d and voted for Node %d\n", rf->me, term, senderId);
        MPI_Send(&rf->currentTerm, 1, MPI_INT, senderId, VOTE_RESPONSE_TAG, MPI_COMM_WORLD); // Send vote response
        rf->currentTerm = term; // Update term
        rf->timeout = MPI_Wtime() + rf->electionTimeout; // reset timeout after vote
    } else {
        // If the term is the same or less, do not vote
        MPI_Send(&rf->currentTerm, 1, MPI_INT, senderId, VOTE_RESPONSE_TAG, MPI_COMM_WORLD);
    }
}

// Handle heartbeat
void handleHeartbeat(Raft *rf, int term, int senderId) {
    if (term >= rf->currentTerm) {
        rf->currentTerm = term; // ensure the term is up to date
        rf->state = FOLLOWER;
        rf->votedFor = -1;
        printf("Node %d (FOLLOWER) received heartbeat from Node %d (LEADER), and is up to date to Term %d\n", rf->me, senderId, term);
        
        rf->heartbeatReceived = true;
        rf->lastHeartbeatTime = MPI_Wtime(); // Reset heartbeat time on areceiving heartbeat
        rf->timeout = MPI_Wtime() + rf->electionTimeout; // reset timeout
    }
}

void getVoteRequest(Raft *rf) {
    int flag = 0;
    int term;
    MPI_Status status;

    MPI_Iprobe(MPI_ANY_SOURCE, VOTE_REQUEST_TAG, MPI_COMM_WORLD, &flag, &status);
    if (flag) { // received vote request
        MPI_Recv(&term, 1, MPI_INT, status.MPI_SOURCE, VOTE_REQUEST_TAG, MPI_COMM_WORLD, &status);
        // Check for vote requests and handle them            
        handleVoteRequest(rf, term, status.MPI_SOURCE);
    } 
}

void getHeartbeat(Raft *rf) {
    int flag = 0;
    int term;
    MPI_Status status;

    MPI_Iprobe(MPI_ANY_SOURCE, HEARTBEAT_TAG, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
        MPI_Recv(&term, 1, MPI_INT, status.MPI_SOURCE, HEARTBEAT_TAG, MPI_COMM_WORLD, &status);
        if (term >= rf->currentTerm) { // normal heartbeat receiving & catchup
            handleHeartbeat(rf, term, status.MPI_SOURCE);
        } else { // received heartbeat from former leader
            printf("Node %d ignored heartbeat from Node %d with lower term %d\n", rf->me, status.MPI_SOURCE, term);
        }
    }
}

void isTimeout(Raft *rf, int numNodes) {
    if (!rf->heartbeatReceived && MPI_Wtime() > rf->timeout) {
        // No heartbeat received within timeout, start election
        printf("Node %d has not received a heartbeat for %.2f seconds, becoming candidate...\n", rf->me, rf->electionTimeout);
        startElection(rf, numNodes);
    }
}

void handleProposal(Raft *rf, Proposal *newProposal, int senderId) {
    double randValue = (double)rand() / RAND_MAX;
    if (randValue < FOLLOWER_FAILURE_PROBABILITY) { // Simulate follower failure based on a probability
        printf("Node %d (Follower) has failed to acknowledge leader's proposal (simulated failure)\n", rf->me);
        int failedAck = -1;
        MPI_Send(&failedAck, 1, MPI_INT, senderId, ACK_TAG, MPI_COMM_WORLD);
    } else {
        MPI_Send(&newProposal->value, 1, MPI_INT, senderId, ACK_TAG, MPI_COMM_WORLD);
    }
    rf->proposalMade = newProposal->proposalMade;
    rf->decisionMade = newProposal->decisionMade;
}

void getProposal(Raft *rf) {
    int flag = 0;
    Proposal newProposal;
    MPI_Status status;

    MPI_Iprobe(MPI_ANY_SOURCE, VALUE_TAG, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
        MPI_Recv(&newProposal, sizeof(Proposal), MPI_BYTE, status.MPI_SOURCE, VALUE_TAG, MPI_COMM_WORLD, &status);
        handleProposal(rf, &newProposal, status.MPI_SOURCE);
    }
}

void logSpeed(int leaderID, int value, int term, double leaderStartTime, int decisionMade) {
    FILE *logFile;

    logFile = fopen("speed_log.txt", "a"); // append for subsequent calls

    if (logFile == NULL) {
        printf("Error opening speed_log.txt\n");
        return;
    }

    // calculate elapsed time since leader election
    double curTime = MPI_Wtime();
    double timeTaken = curTime - leaderStartTime;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", t);

    // log data
    fprintf(logFile, "%s Leader %d committed a value %d in term %d, taking %.5f seconds to reach the decision\n", timeStr, leaderID, value, term, timeTaken);
    
    fflush(logFile);
    fclose(logFile);
}

void logLeader(int leaderID, int term) {
    FILE *logFile = fopen("leader_log.txt", "a");

    if (logFile == NULL) {
        printf("Error opening speed_log.txt\n");
        return;
    }

    fprintf(logFile, "Leader %d became a leader in term %d\n", leaderID, term);
    fflush(logFile);
    fclose(logFile);
}

// Main function
int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int numNodes;
    MPI_Comm_size(MPI_COMM_WORLD, &numNodes);
    int nodeId;
    MPI_Comm_rank(MPI_COMM_WORLD, &nodeId);

    Raft raft;
    initializeRaft(&raft, nodeId, numNodes);

    raft.timeout = MPI_Wtime() + raft.electionTimeout; // next time to start leaader election

    while (1) {       
        if (raft.decisionMade >= 10) {
            printf("Node %d: 10 decisions have been made. Terminating...\n", raft.me);
            MPI_Abort(MPI_COMM_WORLD, 0);
            return 0;
        }

        if (raft.state == LEADER) {

            // Send proposal to followers    
            int proposedValue;       
            if (!raft.proposed) {
                proposedValue = 42 + raft.proposalMade;
                Proposal newProposal = {raft.proposalMade + 1, raft.decisionMade, proposedValue};

                printf("Node %d (Leader) is proposing client entered value %d.\n", raft.me, proposedValue);
                raft.leaderStartTime = MPI_Wtime();
                for (int i = 0; i < numNodes; i++) {
                    if (i != raft.me) {
                        MPI_Send(&newProposal, sizeof(Proposal), MPI_BYTE, i, VALUE_TAG, MPI_COMM_WORLD);
                    }
                }
                raft.proposed = true;
                raft.ackReceived = 1;
                raft.ackCount = 1;

                raft.proposalMade++;
            }

            // Handle acknowledgement from followers
            int flag = 0;
            MPI_Status status;
            MPI_Iprobe(MPI_ANY_SOURCE, ACK_TAG, MPI_COMM_WORLD, &flag, &status);
            if (flag) { // Received acknowledgement
                int ack;
                MPI_Recv(&ack, 1, MPI_INT, status.MPI_SOURCE, ACK_TAG, MPI_COMM_WORLD, &status);                
                
                if (ack == proposedValue) { // Proposal acknowledged correctly
                    raft.ackCount++;
                    printf("Node %d responded with value %d.\n", status.MPI_SOURCE, ack);
                    raft.ackReceived++;
                } else if (ack == -1) { // failed acknowledgement
                    raft.ackReceived++;
                }
            }

            // Check if quorum is reached
            if (raft.ackCount > numNodes / 2) {
                printf("Quorum is reached with the value %d committed.\n", proposedValue);
                logSpeed(raft.me, proposedValue, raft.currentTerm, raft.leaderStartTime, raft.decisionMade);
                raft.decisionMade++;
                raft.lastCommittedValue = proposedValue;
                raft.proposed = false;
            } else if (raft.ackReceived == numNodes) {
                printf("Quorum is not reached for the proposed value %d.\n", proposedValue);
                raft.proposed = false;
            }

            // Handle vote request from candidate
            getVoteRequest(&raft);

            // Check for heartbeat messages
            getHeartbeat(&raft);
            
            double randValue = (double)rand() / RAND_MAX;
            if (randValue < LEADER_FAILURE_PROBABILITY) { // Simulate leader failure based on a probability
                printf("Node %d (Leader) has failed to send heartbeat (simulated failure)\n", raft.me);
                raft.state = FOLLOWER; // Simulate leader failure and transition to follower state
                usleep(1000000);
                raft.timeout = MPI_Wtime() + raft.electionTimeout;
            } else { // Leader sends heartbeat periodically
                for (int i = 0; i < numNodes; i++) {
                    if (i != raft.me) { // Don't send to self
                        int term = raft.currentTerm;
                        MPI_Send(&term, 1, MPI_INT, i, HEARTBEAT_TAG, MPI_COMM_WORLD);
                    }
                    printf("Node %d (Leader) sent heartbeat to followers with term %d\n", raft.me, raft.currentTerm);
                }

                usleep(100000); // Sleep for 100 ms to avoid overwhelming followers with heartbeats
                raft.lastHeartbeatTime = MPI_Wtime(); // Update leader heartbeat send time
            }
            
        } else if (raft.state == FOLLOWER) {
            raft.proposed = false;
            raft.ackCount = 0;
            raft.ackReceived = 0;
            raft.heartbeatReceived = false; // reset hearbeat

            // Check for vote requests and handle them
            getVoteRequest(&raft);

            // Check for heartbeat messages
            getHeartbeat(&raft);

            // Check for proposal message
            getProposal(&raft);

            // Check for timeout
            isTimeout(&raft, numNodes);

        } else { 
            // CANDIDATE logic
            MPI_Status status;
            int term;
            int flag = 0;

            // Candidate awaits vote responses
            MPI_Iprobe(MPI_ANY_SOURCE, VOTE_RESPONSE_TAG, MPI_COMM_WORLD, &flag, &status);
            if (flag) { // Received vote
                MPI_Recv(&term, 1, MPI_INT, status.MPI_SOURCE, VOTE_RESPONSE_TAG, MPI_COMM_WORLD, &status);
                if (term < raft.currentTerm) {
                    raft.voteCount++;
                    printf("Node %d (Term %d) has received %d vote(s) so far.\n", raft.me, raft.currentTerm, raft.voteCount);
                } else if (term > raft.currentTerm) {
                    // If term from the received vote is higher, become FOLLOWER
                    raft.currentTerm = term;
                    raft.state = FOLLOWER;
                    printf("Node %d became a FOLLOWER (Term %d) after noticing higher term\n", raft.me, term);
                    raft.timeout = MPI_Wtime() + raft.electionTimeout;
                }
                raft.received++;
            }

            if (raft.received == numNodes) { // All votes received
                // Check if candidate has won the election
                if (raft.voteCount > numNodes / 2) {
                    printf("Node %d has won the election and is now the LEADER\n", raft.me);
                    raft.state = LEADER;
                    logLeader(raft.me, raft.currentTerm);
                } else {
                    printf("Node %d failed to win the election and becomes FOLLOWER\n", raft.me);
                    raft.state = FOLLOWER;
                }
                raft.timeout = MPI_Wtime() + raft.electionTimeout; // Reset timeout
            }

            // Handle vote request from other candidates
            getVoteRequest(&raft);

            // Check for heartbeat messages
            getHeartbeat(&raft);

            // Check for timeout
            isTimeout(&raft, numNodes);
        }
    }

    MPI_Finalize();
    return 0;
}
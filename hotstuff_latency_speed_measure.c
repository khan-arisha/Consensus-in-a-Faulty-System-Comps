/*
 * Regular hotstuf with the function separated for each role.
 */

#include <mpi.h>
#include <string.h> // Add this line to use memset

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdarg.h>
#define LOGGER_H

#include <time.h>

double total_latency = 0.0;
int msg_count = 0;

double start = 0;

double end = 0;

  double startSpeed = 0.0;

    double endSpeed = 0.0;

// for writing to each node
void write_log(int node_id, const char *format, ...);

void write_log2(const char *format, ...);

// Log for each note
void write_log(int node_id, const char *format, ...)
{
    char filename[50];
    snprintf(filename, sizeof(filename), "node_%d_log.txt", node_id); // Create unique log file per node

    FILE *logfile = fopen(filename, "a"); // Append mode
    if (logfile == NULL)
    {
        perror("Error opening log file");
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(logfile, format, args);
    fprintf(logfile, "\n");
    va_end(args);

    fclose(logfile);
}

void write_log2(const char *format, ...)
{
    FILE *logfile = fopen("hotstuff_log.txt", "a"); // Append mode
    if (logfile == NULL)
    {
        perror("Error opening log file");
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(logfile, format, args);
    fprintf(logfile, "\n");
    va_end(args);

    fclose(logfile);
}

#define NUM_NODES 3        // Total number of nodes
#define BYZANTINE_FAULTS 1 // Number of Byzantine nodes allowed (up to 3)

// Data Structures
typedef struct hStuff hStuff;

typedef struct QC
{
    int viewNumber;
    hStuff *node;
} QC;

typedef struct hStuff
{
    int viewNum;   // last frame seen
    int nodeID;    // Node ID
    bool state;    // leader or not
    int value;     // actual value the node will hold
    bool isFaulty; // true = faulty, false = honest
    int leaderNodeID;
    QC *prepareQC;
    QC *lockedQC;
} hStuff;

typedef struct Message
{
    int viewNumber;
    int proposedVal;
    int nodeID;
    QC *justify;
} Message;

void Msg(Message *m, int proposedVal, int viewNumber, QC *justify)
{
    m->proposedVal = proposedVal;
    m->viewNumber = viewNumber;
    m->justify = justify;
}

// Summary structure
typedef struct
{
    int term;
    int leaderID;
    bool prepareQuorumReached;
    bool precommitQuorumReached;
    bool commitQuorumReached;
    bool DecisionQuorumReached;
    int finalVal;
    int *lyingNodes;     // Track nodes that lied
    int *nodeValues;     // Track what value each node sent
    int lyingNodeCount;  // Counter for lying nodes
    int nodeValuesCount; // Counter for node values
    int realVal;         // Counter for node values

} TermSummary;

void initializeSummary(TermSummary *summary, int term)
{
    summary->term = term;
    summary->leaderID = 0;
    summary->prepareQuorumReached = false;
    summary->precommitQuorumReached = false;
    summary->commitQuorumReached = false;
    summary->DecisionQuorumReached = false;

    summary->finalVal = 0;
    summary->lyingNodeCount = 0;
    summary->nodeValuesCount = 0;

    summary->lyingNodes = malloc(sizeof(int) * 50);
    summary->nodeValues = malloc(sizeof(int) * 50);

    // Initialize nodeValues with default value
    for (int i = 0; i < 50; i++)
    {
        summary->nodeValues[i] = -1;
    }

    for (int i = 0; i < 50; i++)
    {
        summary->lyingNodes[i] = -1;
    }

    summary->realVal = 0;
}

void printSummary(hStuff *hs, const TermSummary *summary)
{
    MPI_Barrier(MPI_COMM_WORLD);

    if (hs->nodeID == 0)
    {
        printf("\n=== Term %d, Leader %d ===\n", summary->term, summary->leaderID);

        if (summary->precommitQuorumReached && summary->commitQuorumReached &&
            summary->DecisionQuorumReached && summary->prepareQuorumReached)
        {
            printf("All phases reached quorum ✅\n");
        }
        else
        {
            printf("The following quorums were not reached:\n");
            if (!summary->precommitQuorumReached)
            {
                printf("- precommitQuorum\n");
            }
            if (!summary->commitQuorumReached)
            {
                printf("- commitQuorum\n");
            }
            if (!summary->DecisionQuorumReached)
            {
                printf("- DecisionQuorum\n");
            }
            if (!summary->prepareQuorumReached)
            {
                printf("- prepareQuorum\n");
            }
        }

        // printf("Lying nodes (sent incorrect values): ");
        for (int i = 0; i < BYZANTINE_FAULTS; i++)
        {
            int nodeID = summary->lyingNodes[i];

            int valueSent = summary->nodeValues[nodeID]; // value this node sent
            printf("Lying Node %d: Sent message %d 🐍\n", nodeID, valueSent);
        }

        // printf("\n");

        printf("Node values sent: ");
        for (int i = 0; i < 3; i++)
        {
            printf("%d ", summary->nodeValues[i]);
        }

        printf("\nAll honest nodes sent %d 👍\n", summary->realVal);

        printf("Nodes logged value %d 📝", summary->finalVal);

        printf("\n=========================\n");
    }
    MPI_Barrier(MPI_COMM_WORLD);
}

void initializeHotGrapez(hStuff *hs, int nodeId, int numNodes, TermSummary *summary)
{
    startSpeed = MPI_Wtime();
    hs->viewNum = 0;
    hs->nodeID = nodeId;

    // Set the first leader
    hs->leaderNodeID = 0;
    if (hs->nodeID == 0)
    {
        hs->state = true;
    }
    else
    {
        hs->state = false;
    }

    // Set first Byzantine nodes to faulty
    if (nodeId < BYZANTINE_FAULTS)
    {
        hs->isFaulty = true;
        summary->lyingNodes[summary->lyingNodeCount++] = hs->nodeID;
    }
    else
    {
        hs->isFaulty = false;
    }
}
void decide_follower(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary);

void startNewTerm_Leader(hStuff *hs, int term, int world_size, TermSummary *summary)
{

    // Leader sends the new term to all other nodes
    if (hs->state)
    {
        write_log(hs->nodeID, "Leader %d: Broadcasting new term %d to all nodes. \n", hs->nodeID, term);
        write_log2("Leader %d: Broadcasting new term %d to all nodes. \n", hs->nodeID, term);
        // printf("Leader %d: Broadcasting new term %d to all nodes.\n", hs->nodeID, term);
        for (int i = 0; i < world_size; i++)
        {
            if (i != hs->nodeID)
            {
                start = MPI_Wtime();
                MPI_Send(&term, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            }
        }
    }
}

void startNewTerm_Follower(hStuff *hs, int term, int world_size, TermSummary *summary)
{
    // Leader sends the new term to all other nodes
    if (!(hs->state))
    {
        // Follower logic
        int receivedTerm;
        MPI_Recv(&receivedTerm, 1, MPI_INT, hs->leaderNodeID, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        end = MPI_Wtime();
        double latency = end - start;
        total_latency += latency;
        msg_count++;

        write_log(hs->nodeID, "Node %d: Received new term %d from Leader. \n", hs->nodeID, term);
        write_log2("Node %d: Received new term %d from Leader. \n", hs->nodeID, term);
        // printf("Node %d: Received new term %d from Leader.\n", hs->nodeID, receivedTerm);

        // Send back term acknowledgment
        if (receivedTerm >= hs->viewNum)
        {
            hs->viewNum = receivedTerm; // update view
            start = MPI_Wtime();
            MPI_Send(&receivedTerm, 1, MPI_INT, hs->leaderNodeID, 1, MPI_COMM_WORLD);
        }
    }
}

void prepare_leader(hStuff *hs, int term, int world_size, TermSummary *summary)
{
    int received_terms[world_size];
    int quorum = NUM_NODES - ((NUM_NODES - 1 + 2) / 3); // n - f (quorum size)
    int ackCount = 0;                                   // acknowledgment count

    if (hs->state)
    {
        // Leader waits for acknowledgments
        write_log(hs->nodeID, "Leader %d: Waiting for followers to acknowledge new term %d. \n", hs->nodeID, term);
        write_log2("Leader %d: Waiting for followers to acknowledge new term %d. \n", hs->nodeID, term);
        // printf("Leader %d: Waiting for followers to acknowledge new term %d.\n", hs->nodeID, term);

        for (int i = 0; i < world_size - 1; i++)
        {
            int ack;
            MPI_Recv(&ack, 1, MPI_INT, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            end = MPI_Wtime();
            double latency = end - start;
            total_latency += latency;
            msg_count++;

            if (ack == term)
            {
                ackCount++;
            }
        }

        if (ackCount >= quorum)
        {
            write_log(hs->nodeID, "Leader %d: Quorum reached. Preparing a PREPARE message for term %d. \n", hs->nodeID, term);
            write_log2("Leader %d: Quorum reached. Preparing a PREPARE message for term %d. \n", hs->nodeID, term);
            // printf("Leader %d: Quorum reached. Preparing a PREPARE message for term %d.\n", hs->nodeID, term);
            summary->prepareQuorumReached = true;

            Message prepareMsg;
            QC qc = {term, hs};
            Msg(&prepareMsg, 42, term, &qc); // for ex: proposedVal = 42

            // Broadcast the prepare msg
            for (int i = 0; i < world_size; i++)
            {
                if (i != hs->nodeID)
                {
                    start = MPI_Wtime();
                    MPI_Send(&prepareMsg, sizeof(Message), MPI_BYTE, i, 2, MPI_COMM_WORLD);
                }
            }
        }
        else
        {
            write_log(hs->nodeID, "Leader %d: Quorum not reached. Cannot proceed with PREPARE for term %d. \n", hs->nodeID, term);
            write_log2("Leader %d: Quorum not reached. Cannot proceed with PREPARE for term %d. \n", hs->nodeID, term);
            // printf("Leader: Quorum not reached. Cannot proceed with PREPARE.\n");
        }
    }
}

void prepare_follower(hStuff *hs, int term, int world_size, TermSummary *summary)
{
    if (!(hs->state))
    {
        // Follower logic
        Message prepareMsg;
        MPI_Recv(&prepareMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        end = MPI_Wtime();
        double latency = end - start;
        total_latency += latency;
        msg_count++;

        if (prepareMsg.viewNumber == term)
        {
            Message faultyMsg = prepareMsg;
            faultyMsg.nodeID = hs->nodeID;
            prepareMsg.nodeID = hs->nodeID;
            if (hs->isFaulty == true)
            {
                write_log(hs->nodeID, "Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                write_log2("Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                // printf("Faulty Node %d: Received PREPARE message for term %d. Sending Faulty PREPARE vote.\n", hs->nodeID, term);
                faultyMsg.proposedVal = 2;
                start = MPI_Wtime();
                MPI_Send(&faultyMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 3, MPI_COMM_WORLD);
            }
            else
            {
                write_log(hs->nodeID, "Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                write_log2("Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                // printf("Node %d: Received PREPARE message for term %d. Sending PREPARE vote.\n", hs->nodeID, term);
                start = MPI_Wtime();
                MPI_Send(&prepareMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 3, MPI_COMM_WORLD);
            }
        }
    }
}

void precommit_leader(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary)
{
    if (hs->state)
    {
        write_log(hs->nodeID, "Leader %d: Waiting for PREPARE votes for term %d. \n", hs->nodeID, term);
        write_log2("Leader %d: Waiting for PREPARE votes for term %d. \n", hs->nodeID, term);
        // printf("Leader %d: Waiting for PREPARE votes for term %d.\n", hs->nodeID, term);
        int quorum = NUM_NODES - ((NUM_NODES - 1 + 2) / 3);
        int voteCount = 0;
        summary->precommitQuorumReached = true;

        for (int i = 0; i < world_size - 1; i++)
        {
            Message receivedVote;

            MPI_Recv(&receivedVote, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            end = MPI_Wtime();
            double latency = end - start;
            total_latency += latency;
            msg_count++;

            // printf("Leader: received prepare vote from node %d with viewNumber %d and proposed value %d.\n", receivedVote.nodeID, receivedVote.viewNumber, receivedVote.proposedVal);

            if (receivedVote.viewNumber == term)
            {
                voteCount++;
            }
        }

        if (voteCount >= quorum)
        {
            write_log(hs->nodeID, "Leader %d: Quorum reached for PRE-COMMIT. Broadcasting PRE-COMMIT message for term %d. \n", hs->nodeID, term);
            write_log2("Leader %d: Quorum reached for PRE-COMMIT. Broadcasting PRE-COMMIT message for term %d. \n", hs->nodeID, term);
            // printf("Leader %d: Quorum reached for PRE-COMMIT. Broadcasting PRE-COMMIT message.\n", hs->nodeID);
            Message precommitMsg;
            QC qc = {term, hs};
            hs->prepareQC = &qc;
            Msg(&precommitMsg, proposedVal, term, &qc);

            for (int i = 0; i < world_size; i++)
            {
                if (i != hs->nodeID)
                {
                    start = MPI_Wtime();
                    MPI_Send(&precommitMsg, sizeof(Message), MPI_BYTE, i, 4, MPI_COMM_WORLD);
                }
            }

            if (hs->isFaulty == true)
            {
                precommitMsg.proposedVal = 2;
                summary->lyingNodes[summary->lyingNodeCount++] = hs->nodeID;
                summary->nodeValues[hs->nodeID] = precommitMsg.proposedVal;
            }
        }
        else
        {
            write_log(hs->nodeID, "Leader %d: Quorum not reached for PRE-COMMIT for term %d. \n", hs->nodeID, term);
            write_log2("Leader %d: Quorum not reached for PRE-COMMIT for term %d. \n", hs->nodeID, term);
            // printf("Leader: Quorum not reached for PRE-COMMIT.\n");
        }
    }
}

void precommit_follower(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary)
{
    if (!(hs->state))
    {
        Message precommitMsg;
        MPI_Recv(&precommitMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        end = MPI_Wtime();
        double latency = end - start;
        total_latency += latency;
        msg_count++;

        if (precommitMsg.viewNumber == term)
        {
            Message faultyMsg = precommitMsg;
            faultyMsg.nodeID = hs->nodeID;
            precommitMsg.nodeID = hs->nodeID;

            hs->prepareQC = precommitMsg.justify;
            if (hs->isFaulty == true)
            {
                write_log(hs->nodeID, "Node %d: Received PRE-COMMIT message. Sending PRE-COMMIT vote for term %d.\n", hs->nodeID, term);
                write_log2("Node %d: Received PRE-COMMIT message. Sending PRE-COMMIT vote for term %d.\n", hs->nodeID, term);
                // printf("Node %d: Received PRE-COMMIT message for term %d. Sending PRE-COMMIT vote.\n", hs->nodeID, term);
                faultyMsg.proposedVal = 2;
                start = MPI_Wtime();
                MPI_Send(&faultyMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 5, MPI_COMM_WORLD);
            }
            else
            {
                write_log(hs->nodeID, "Node %d: Received PRE-COMMIT message. Sending PRE-COMMIT vote for term %d.\n", hs->nodeID, term);
                write_log2("Node %d: Received PRE-COMMIT message. Sending PRE-COMMIT vote for term %d.\n", hs->nodeID, term);
                // printf("Node %d: Received PRE-COMMIT message for term %d. Sending PRE-COMMIT vote.\n", hs->nodeID, term);
                start = MPI_Wtime();
                MPI_Send(&precommitMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 5, MPI_COMM_WORLD);
            }
        }
    }
}

void commit_leader(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary)
{
    if (hs->state)
    { // Leader logic
        write_log(hs->nodeID, "Leader %d: Waiting for PRE-COMMIT votes for term %d. \n", hs->nodeID, term);
        write_log2("Leader %d: Waiting for PRE-COMMIT votes for term %d. \n", hs->nodeID, term);
        // printf("Leader %d: Waiting for PRE-COMMIT votes for term %d.\n", hs->nodeID, term);
        int quorum = NUM_NODES - ((NUM_NODES - 1 + 2) / 3);
        int voteCount = 0;

        for (int i = 0; i < world_size - 1; i++)
        {
            Message receivedVote;
            MPI_Recv(&receivedVote, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, 5, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            end = MPI_Wtime();
            double latency = end - start;
            total_latency += latency;
            msg_count++;

            // printf("Leader: received precommit vote from node %d and precommit message %d.\n", receivedVote.nodeID, receivedVote.proposedVal);

            if (receivedVote.viewNumber == term)
            {
                voteCount++;
            }
        }

        if (voteCount >= quorum)
        {
            write_log(hs->nodeID, "Leader %d: Quorum reached for COMMIT. Broadcasting COMMIT message for term %d. \n", hs->nodeID, term);
            write_log2("Leader %d: Quorum reached for COMMIT. Broadcasting COMMIT message for term %d. \n", hs->nodeID, term);
            // printf("Leader %d: Quorum reached for COMMIT. Broadcasting COMMIT message.\n", hs->nodeID);
            summary->commitQuorumReached = true;

            Message commitMsg;
            QC qc = {term, hs};
            Msg(&commitMsg, proposedVal, term, &qc);

            for (int i = 0; i < world_size; i++)
            {
                if (i != hs->nodeID)
                {
                    start = MPI_Wtime();
                    MPI_Send(&commitMsg, sizeof(Message), MPI_BYTE, i, 6, MPI_COMM_WORLD);
                }
            }
        }
        else
        {
            write_log(hs->nodeID, "Leader %d: Quorum not reached for COMMIT for term %d. \n", hs->nodeID, term);
            write_log2("Leader %d: Quorum not reached for COMMIT for term %d. \n", hs->nodeID, term);
            // printf("Leader: Quorum not reached for COMMIT.\n");
        }
    }
}

void commit_follower(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary)
{
    summary->realVal = proposedVal;
    if (!(hs->state))
    {
        Message commitMsg;
        MPI_Recv(&commitMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 6, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (commitMsg.viewNumber == term)
        {
            Message faultyMsg = commitMsg;
            faultyMsg.nodeID = hs->nodeID;
            commitMsg.nodeID = hs->nodeID;
            hs->lockedQC = commitMsg.justify;
            if (hs->isFaulty == true)
            {
                write_log(hs->nodeID, "Node %d: Received COMMIT message for term %d. Sending COMMIT vote for term %d.\n", hs->nodeID, term);
                write_log2("Node %d: Received COMMIT message for term %d. Sending COMMIT vote for term %d.\n", hs->nodeID, term);
                // printf("Node %d: Received COMMIT message for term %d. Sending COMMIT vote.\n", hs->nodeID, term);
                faultyMsg.proposedVal = 2;
                start = MPI_Wtime();
                MPI_Send(&faultyMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 7, MPI_COMM_WORLD);
                summary->lyingNodes[summary->lyingNodeCount++] = hs->nodeID;
                summary->nodeValues[hs->nodeID] = faultyMsg.proposedVal;
            }
            else
            {
                write_log(hs->nodeID, "Node %d: Received COMMIT message for term %d. Sending COMMIT vote for term %d.\n", hs->nodeID, term);
                write_log2("Node %d: Received COMMIT message for term %d. Sending COMMIT vote for term %d.\n", hs->nodeID, term);
                // printf("Node %d: Received COMMIT message for term %d. Sending COMMIT vote.\n", hs->nodeID, term);
                start = MPI_Wtime();
                MPI_Send(&commitMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 7, MPI_COMM_WORLD);
                summary->nodeValues[hs->nodeID] = proposedVal;
            }
        }
    }
}

void decide_leader(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary)
{
    if (hs->state)
    {
        write_log(hs->nodeID, "Leader %d: Waiting for COMMIT votes for term %d. \n", hs->nodeID, term);
        write_log2("Leader %d: Waiting for COMMIT votes for term %d. \n", hs->nodeID, term);
        // printf("Leader %d: Waiting for COMMIT votes for term %d.\n", hs->nodeID, term);
        int quorum = NUM_NODES - ((NUM_NODES - 1 + 2) / 3);
        int commitCount = 0;
        int count_42 = 0;
        int count_2 = 0;

        // Consider the leader's own vote first
        if (hs->isFaulty)
        {
            count_2++;
        }
        else
        {
            count_42++;
        }

        for (int i = 0; i < world_size - 1; i++)
        {
            Message receivedCommit;
            MPI_Recv(&receivedCommit, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, 7, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            end = MPI_Wtime();
            double latency = end - start;
            total_latency += latency;
            msg_count++;

            // printf("Leader: received commit vote from node %d and proposed value %d.\n", receivedCommit.nodeID, receivedCommit.proposedVal);

            if (receivedCommit.viewNumber == term)
            {
                commitCount++;
            }
            if (receivedCommit.proposedVal == 42)
            {
                count_42++;
            }
            else
            {
                count_2++;
            }
            summary->nodeValues[receivedCommit.nodeID] = receivedCommit.proposedVal;

            // Debugging the nodeValues array after assignment
            // printf("Debug: After assignment, nodeValues[%d] = %d\n",receivedCommit.nodeID, summary->nodeValues[receivedCommit.nodeID]);

            if (receivedCommit.proposedVal != proposedVal)
            {

                // printf("Node %d lied: Sent value %d instead of %d\n", receivedCommit.nodeID, receivedCommit.proposedVal, proposedVal);
                summary->lyingNodes[summary->lyingNodeCount++] = receivedCommit.nodeID;
            }
        }

        if (commitCount >= quorum)
        {

            write_log(hs->nodeID, "Leader %d: Quorum reached. Broadcasting DECIDE message for term %d. \n", hs->nodeID, term);
            write_log2("Leader %d: Quorum reached. Broadcasting DECIDE message for term %d. \n", hs->nodeID, term);
            // printf("Leader %d: Quorum reached. Broadcasting DECIDE message.\n", hs->nodeID);
            summary->DecisionQuorumReached = true;

            Message decideMsg;
            QC qc = {term, hs};

            if (count_42 > count_2)
            {
                proposedVal = 42;
            }
            else
            {
                proposedVal = 2;
            }
            // printf("count 42 is %d while count 2 is %d \n", count_42, count_2);

            hs->value = proposedVal;
            summary->finalVal = hs->value;
            summary->leaderID = hs->nodeID;
            summary->term = term;

            // *** Leader logs its own decision ***
            write_log(hs->nodeID, "Node %d (Leader): DECISION made for term %d: value = %d.\n", hs->nodeID, term, hs->value);
            write_log2("Node %d (Leader): DECISION made for term %d: value = %d.\n", hs->nodeID, term, hs->value);
            // printf("Node %d (Leader): DECISION made for term %d: value = %d.\n", hs->nodeID, term, hs->value);

            Msg(&decideMsg, proposedVal, term, &qc);

            for (int i = 0; i < world_size; i++)
            {
                if (i != hs->nodeID)
                {
                    start = MPI_Wtime();
                    MPI_Send(&decideMsg, sizeof(Message), MPI_BYTE, i, 8, MPI_COMM_WORLD);
                }
            }
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
}

void decide_follower(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary)
{
    if (!(hs->state))
    {

        Message decideMsg;
        MPI_Recv(&decideMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 8, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        end = MPI_Wtime();
        double latency = end - start;
        total_latency += latency;
        msg_count++;
        if (decideMsg.viewNumber == term)
        {
            hs->value = decideMsg.proposedVal;
            write_log(hs->nodeID, "Node %d: DECISION made for term %d: value = %d.. \n", hs->nodeID, term, hs->value);
            write_log2("Node %d: DECISION made for term %d: value = %d.. \n", hs->nodeID, term, hs->value);
            // printf("Node %d: DECISION made for term %d: value = %d.\n", hs->nodeID, term, hs->value);
            endSpeed = MPI_Wtime();

            summary->finalVal = hs->value;
            summary->leaderID = hs->leaderNodeID;
            summary->term = term;
        }
    }
}

void rotateLeader(hStuff *hs, int world_size)
{
    hs->leaderNodeID = (hs->leaderNodeID + 1) % world_size; // round-robin leader rotation
    if (hs->nodeID == hs->leaderNodeID)
    {
        hs->state = true;
        // prepare(hs, hs->viewNum, world_size);
    }
    else
    {
        hs->state = false;
    }
}

int main(int argc, char **argv)
{
    srand(42);
    MPI_Init(&argc, &argv);
    int byzantine_fault_num = BYZANTINE_FAULTS;

    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    hStuff hs;

    int term = 1;

    TermSummary summary;

    initializeSummary(&summary, term);
    initializeHotGrapez(&hs, rank, world_size, &summary);
    double total_latency_all_terms = 0.0;
    int total_msg_count_all_terms = 0;
    double total_speed = 0.0; // Store total speed across all terms
    int num_terms = 5;        // Number of terms

  

    while (term <= 5)
    { // Run for 5 terms
    srand(42);
        double term_latency = 0.0;
        int term_msg_count = 0;
        msg_count = 0;
        total_latency = 0.0;

        startNewTerm_Leader(&hs, term, world_size, &summary);
        MPI_Barrier(MPI_COMM_WORLD);
        startNewTerm_Follower(&hs, term, world_size, &summary);
        MPI_Barrier(MPI_COMM_WORLD);

        prepare_leader(&hs, term, world_size, &summary);
        MPI_Barrier(MPI_COMM_WORLD);
        prepare_follower(&hs, term, world_size, &summary);
        MPI_Barrier(MPI_COMM_WORLD);

        precommit_leader(&hs, term, 42, world_size, &summary);
        MPI_Barrier(MPI_COMM_WORLD);
        precommit_follower(&hs, term, 42, world_size, &summary);
        MPI_Barrier(MPI_COMM_WORLD);

        commit_leader(&hs, term, 42, world_size, &summary); // Example proposed value = 42
        MPI_Barrier(MPI_COMM_WORLD);
        commit_follower(&hs, term, 42, world_size, &summary); // Example proposed value = 42
        MPI_Barrier(MPI_COMM_WORLD);

        decide_leader(&hs, term, 42, world_size, &summary);
        MPI_Barrier(MPI_COMM_WORLD);
        decide_follower(&hs, term, 42, world_size, &summary);
        MPI_Barrier(MPI_COMM_WORLD);

        rotateLeader(&hs, world_size); // Rotate leader after each term
        MPI_Barrier(MPI_COMM_WORLD);

        printSummary(&hs, &summary); // mpi barrier
        MPI_Barrier(MPI_COMM_WORLD);
        double latency_per_message = total_latency / msg_count;

        double speed = endSpeed - startSpeed;
        total_speed += speed;
        
        //printf("Term %d: Consensus Speed = %f seconds\n", term, speed);

        term++; // Move to next term
    }
    double overall_latency = total_latency_all_terms / total_msg_count_all_terms;
    // printf("%f+", overall_latency);

    double avg_speed = total_speed / num_terms;
printf("Average Consensus Speed Across All Terms: %f seconds\n", avg_speed);

    MPI_Finalize();
    return 0;
}
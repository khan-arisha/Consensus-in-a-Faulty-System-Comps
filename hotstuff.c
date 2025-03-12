/*
 * Regular hotstuf with the function separated for each role.
 */

#include <mpi.h>
#include <string.h> 
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdarg.h>
#define LOGGER_H

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

#define MAX_LINE_LENGTH 1024
#define MAX_NODES 10

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

// read csv structure
typedef struct
{
    int nodes[MAX_NODES];
    int faulty_nodes[MAX_NODES];
    int node_counter;
    int faulty_counter;
} CSVRead;

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

void read_csv(hStuff *hs, CSVRead *config)
{

    config->node_counter = 0;
    config->faulty_counter = 0;

    FILE *fp = fopen("data.csv", "r");
    if (!fp)
    {
        perror("Error opening file");
        return;
    }

    char line[1024];

    // Skip the first line (header)
    if (fgets(line, sizeof(line), fp) == NULL)
    {
        printf("Error reading CSV header\n");
        fclose(fp);
        return;
    }

    // Read the second line (actual data)
    if (fgets(line, sizeof(line), fp) != NULL)
    {
        // printf("Read line from CSV: %s\n", line);  // Debugging: Show raw CSV line

        char *bracket1 = strchr(line, '[');
        char *closing_bracket1 = strchr(bracket1, ']');
        char *bracket2 = strchr(closing_bracket1 + 1, '[');
        char *closing_bracket2 = strchr(bracket2, ']');

        if (config->node_counter >= MAX_NODES || config->faulty_counter >= MAX_NODES)
        {
            printf("Error: Too many nodes or faulty nodes\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
            return;
        }

        if (!bracket1 || !bracket2 || !closing_bracket1 || !closing_bracket2)
        {
            printf("Incorrect CSV format");
            fclose(fp);
            return;
        }

        *closing_bracket1 = '\0';
        *closing_bracket2 = '\0';

        char *token = strtok(bracket1 + 1, ",");
        while (token != NULL && config->node_counter < MAX_NODES)
        {
            // printf("tokens %s\n", token);
            config->nodes[config->node_counter++] = atoi(token);
            token = strtok(NULL, ",]");
        }

        token = strtok(bracket2 + 1, ",]");
        while (token != NULL && config->faulty_counter < MAX_NODES)
        {
            // printf("tokens %s\n", token);
            config->faulty_nodes[config->faulty_counter++] = atoi(token);
            token = strtok(NULL, ",");
        }
    }
    fclose(fp);
}

void checkNodeCount(int world_size, CSVRead *config)
{
    if (world_size != config->node_counter)
    {
        printf("Number of MPI processes, %d\n does not match number of nodes %d\n", world_size, config->node_counter);
        MPI_Abort(MPI_COMM_WORLD, 1);
        exit(1);
    }
}

void printSummary(hStuff *hs, const TermSummary *summary, CSVRead *config)
{
    MPI_Barrier(MPI_COMM_WORLD);

    if (hs->nodeID == 0)
    {
        printf("\n=== Term %d, Leader %d ===\n", summary->term, summary->leaderID);

        if (summary->precommitQuorumReached && summary->commitQuorumReached &&
            summary->DecisionQuorumReached && summary->prepareQuorumReached)
        {
            printf("All phases reached quorum for term count ✅\n");
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
        for (int i = 0; i < config->faulty_counter; i++)
        {
            int nodeID = summary->lyingNodes[i];

            int valueSent = summary->nodeValues[nodeID]; // value this node sent
            printf("Lying Node %d: Sent message %d 🐍\n", nodeID, valueSent);
        }

        printf("All honest nodes sent %d 👍\n", summary->realVal);

        if (summary->finalVal != summary->realVal)
        {
            printf("Consensus was not reached 😔 too many evil lying nodes");
        }
        else
        {
            printf("Nodes logged value %d 📝\n", summary->finalVal);
            printf("Consensus reached! 🎉");
        }

        printf("\n=========================\n");
    }
    MPI_Barrier(MPI_COMM_WORLD);
}

void initializeHotGrapez(hStuff *hs, int nodeId, int numNodes, TermSummary *summary, CSVRead *config)
{
    hs->viewNum = 0;
    hs->nodeID = nodeId;

    // Set the first leader
    hs->leaderNodeID = 0;
    hs->state = (hs->nodeID == 0);

    bool is_faulty = false;
    for (int i = 0; i < config->faulty_counter; i++)
    {
        if (nodeId == config->faulty_nodes[i])
        {
            is_faulty = true;
            break;
        }
    }

    hs->isFaulty = is_faulty;
    if (is_faulty)
    {
        summary->lyingNodes[summary->lyingNodeCount++] = hs->nodeID;
    }
}
void decide_follower(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config);

void startNewTerm_Leader(hStuff *hs, int term, int world_size, TermSummary *summary, CSVRead *config)
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
                MPI_Send(&term, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            }
        }
    }
}

void startNewTerm_Follower(hStuff *hs, int term, int world_size, TermSummary *summary, CSVRead *config)
{
    // Leader sends the new term to all other nodes
    if (!(hs->state))
    {
        // Follower logic
        int receivedTerm;
        MPI_Recv(&receivedTerm, 1, MPI_INT, hs->leaderNodeID, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        write_log(hs->nodeID, "Node %d: Received new term %d from Leader. \n", hs->nodeID, term);
        write_log2("Node %d: Received new term %d from Leader. \n", hs->nodeID, term);
        // printf("Node %d: Received new term %d from Leader.\n", hs->nodeID, receivedTerm);

        // Send back term acknowledgment
        if (receivedTerm >= hs->viewNum)
        {
            hs->viewNum = receivedTerm; // update view
            MPI_Send(&receivedTerm, 1, MPI_INT, hs->leaderNodeID, 1, MPI_COMM_WORLD);
        }
    }
}

void prepare_leader(hStuff *hs, int term, int world_size, TermSummary *summary, CSVRead *config)
{
    int received_terms[world_size];
    int quorum = config->node_counter - ((config->node_counter - 1) / 3); // n - f (quorum size)
    int ackCount = 1;                                                     // acknowledgment count

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

void prepare_follower(hStuff *hs, int term, int world_size, TermSummary *summary, CSVRead *config)
{
    if (!(hs->state))
    {
        // Follower logic
        Message prepareMsg;
        MPI_Recv(&prepareMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

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
                MPI_Send(&faultyMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 3, MPI_COMM_WORLD);
            }
            else
            {
                write_log(hs->nodeID, "Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                write_log2("Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                // printf("Node %d: Received PREPARE message for term %d. Sending PREPARE vote.\n", hs->nodeID, term);
                MPI_Send(&prepareMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 3, MPI_COMM_WORLD);
            }
        }
    }
}

void precommit_leader(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config)
{
    if (hs->state)
    {
        write_log(hs->nodeID, "Leader %d: Waiting for PREPARE votes for term %d. \n", hs->nodeID, term);
        write_log2("Leader %d: Waiting for PREPARE votes for term %d. \n", hs->nodeID, term);
        // printf("Leader %d: Waiting for PREPARE votes for term %d.\n", hs->nodeID, term);
        int quorum = config->node_counter - ((config->node_counter - 1) / 3);
        int voteCount = 1;
        summary->precommitQuorumReached = true;

        for (int i = 0; i < world_size - 1; i++)
        {
            Message receivedVote;
            MPI_Recv(&receivedVote, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

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

void precommit_follower(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config)
{
    if (!(hs->state))
    {
        Message precommitMsg;
        MPI_Recv(&precommitMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

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
                MPI_Send(&faultyMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 5, MPI_COMM_WORLD);
            }
            else
            {
                write_log(hs->nodeID, "Node %d: Received PRE-COMMIT message. Sending PRE-COMMIT vote for term %d.\n", hs->nodeID, term);
                write_log2("Node %d: Received PRE-COMMIT message. Sending PRE-COMMIT vote for term %d.\n", hs->nodeID, term);
                // printf("Node %d: Received PRE-COMMIT message for term %d. Sending PRE-COMMIT vote.\n", hs->nodeID, term);
                MPI_Send(&precommitMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 5, MPI_COMM_WORLD);
            }
        }
    }
}

void commit_leader(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config)
{
    if (hs->state)
    { // Leader logic
        write_log(hs->nodeID, "Leader %d: Waiting for PRE-COMMIT votes for term %d. \n", hs->nodeID, term);
        write_log2("Leader %d: Waiting for PRE-COMMIT votes for term %d. \n", hs->nodeID, term);
        // printf("Leader %d: Waiting for PRE-COMMIT votes for term %d.\n", hs->nodeID, term);
        int quorum = config->node_counter - ((config->node_counter - 1) / 3);
        int voteCount = 1;

        for (int i = 0; i < world_size - 1; i++)
        {
            Message receivedVote;
            MPI_Recv(&receivedVote, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, 5, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

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

void commit_follower(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config)
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
                MPI_Send(&faultyMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 7, MPI_COMM_WORLD);
                summary->lyingNodes[summary->lyingNodeCount++] = hs->nodeID;
                summary->nodeValues[hs->nodeID] = faultyMsg.proposedVal;
            }
            else
            {
                write_log(hs->nodeID, "Node %d: Received COMMIT message for term %d. Sending COMMIT vote for term %d.\n", hs->nodeID, term);
                write_log2("Node %d: Received COMMIT message for term %d. Sending COMMIT vote for term %d.\n", hs->nodeID, term);
                // printf("Node %d: Received COMMIT message for term %d. Sending COMMIT vote.\n", hs->nodeID, term);
                MPI_Send(&commitMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 7, MPI_COMM_WORLD);
                summary->nodeValues[hs->nodeID] = proposedVal;
            }
        }
    }
}

void decide_leader(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config)
{
    if (hs->state)
    {
        write_log(hs->nodeID, "Leader %d: Waiting for COMMIT votes for term %d. \n", hs->nodeID, term);
        write_log2("Leader %d: Waiting for COMMIT votes for term %d. \n", hs->nodeID, term);
        // printf("Leader %d: Waiting for COMMIT votes for term %d.\n", hs->nodeID, term);
        int quorum = config->node_counter - ((config->node_counter - 1) / 3);
        int commitCount = 1;
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

            if (count_42 > count_2 + 1) // honest nodes need to be a supermajority
            {
                proposedVal = 42;
            }
            else
            {
                proposedVal = 2;
            }

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
                    MPI_Send(&decideMsg, sizeof(Message), MPI_BYTE, i, 8, MPI_COMM_WORLD);
                }
            }
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
}

void decide_follower(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config)
{
    if (!(hs->state))
    {

        Message decideMsg;
        MPI_Recv(&decideMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 8, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (decideMsg.viewNumber == term)
        {
            hs->value = decideMsg.proposedVal;
            write_log(hs->nodeID, "Node %d: DECISION made for term %d: value = %d.. \n", hs->nodeID, term, hs->value);
            write_log2("Node %d: DECISION made for term %d: value = %d.. \n", hs->nodeID, term, hs->value);
            // printf("Node %d: DECISION made for term %d: value = %d.\n", hs->nodeID, term, hs->value);

            summary->finalVal = hs->value;
            summary->leaderID = hs->leaderNodeID;
            summary->term = term;
        }
    }
}

// round-robin leader rotation
void rotateLeader(hStuff *hs, int world_size, CSVRead *config)
{
    hs->leaderNodeID = (hs->leaderNodeID + 1) % config->node_counter; 
    if (hs->nodeID == hs->leaderNodeID)
    {
        hs->state = true;
    }
    else
    {
        hs->state = false;
    }
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    hStuff hs;
    CSVRead config;

    int term = 1;

    TermSummary summary;

    read_csv(&hs, &config);
    if (rank == 0)
    {
        checkNodeCount(world_size, &config);
    }

    MPI_Barrier(MPI_COMM_WORLD); // mpi barrier helps synchronize everything
    initializeSummary(&summary, term);
    initializeHotGrapez(&hs, rank, world_size, &summary, &config);
    while (term <= 5)
    { // Run for 5 terms
        startNewTerm_Leader(&hs, term, world_size, &summary, &config);
        MPI_Barrier(MPI_COMM_WORLD);
        startNewTerm_Follower(&hs, term, world_size, &summary, &config);
        MPI_Barrier(MPI_COMM_WORLD); 

        prepare_leader(&hs, term, world_size, &summary, &config);
        MPI_Barrier(MPI_COMM_WORLD);
        prepare_follower(&hs, term, world_size, &summary, &config);
        MPI_Barrier(MPI_COMM_WORLD);

        precommit_leader(&hs, term, 42, world_size, &summary, &config); // Example proposed value = 42
        MPI_Barrier(MPI_COMM_WORLD);
        precommit_follower(&hs, term, 42, world_size, &summary, &config); // Example proposed value = 42
        MPI_Barrier(MPI_COMM_WORLD);

        commit_leader(&hs, term, 42, world_size, &summary, &config); 
        MPI_Barrier(MPI_COMM_WORLD);
        commit_follower(&hs, term, 42, world_size, &summary, &config); 
        MPI_Barrier(MPI_COMM_WORLD);

        decide_leader(&hs, term, 42, world_size, &summary, &config);
        MPI_Barrier(MPI_COMM_WORLD);
        decide_follower(&hs, term, 42, world_size, &summary, &config);
        MPI_Barrier(MPI_COMM_WORLD);

        rotateLeader(&hs, world_size, &config); // Rotate leader after each term
        MPI_Barrier(MPI_COMM_WORLD);

        printSummary(&hs, &summary, &config); 
        MPI_Barrier(MPI_COMM_WORLD);

        term++;
    }

    MPI_Finalize();
    return 0;
}
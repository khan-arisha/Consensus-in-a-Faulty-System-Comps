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
 
 //read csv structure
 typedef struct {
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
     int nodeValuesCount; // Counter for node values
     int realVal; // Counter for node values
 
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
 
 void read_csv(hStuff *hs, CSVRead *config) {
 
     config -> node_counter = 0;
     config -> faulty_counter = 0;
 
     FILE* fp = fopen("data.csv", "r");
     if (!fp) {
         perror("Error opening file");
         return;
     }
 
     char line[1024];
 
     // Skip the first line (header)
     if (fgets(line, sizeof(line), fp) == NULL) {
         printf("Error reading CSV header\n");
         fclose(fp);
         return;
     }
 
     // Read the second line (actual data)
     if (fgets(line, sizeof(line), fp) != NULL) {
         // printf("Read line from CSV: %s\n", line);  // Debugging: Show raw CSV line
 
         char *bracket1 = strchr(line, '[');
         char *closing_bracket1 = strchr(bracket1,']');
         char *bracket2 = strchr(closing_bracket1 + 1, '[');
         char *closing_bracket2 = strchr(bracket2, ']');
 
 
         if (config->node_counter >= MAX_NODES || config->faulty_counter >= MAX_NODES) {
         printf("Error: Too many nodes or faulty nodes\n");
         MPI_Abort(MPI_COMM_WORLD, 1);
         return;
         }
 
         if (!bracket1 || !bracket2 || !closing_bracket1 || !closing_bracket2){
             printf("Incorrect CSV format");
             fclose(fp);
             return;
         }
 
         *closing_bracket1 = '\0';
         *closing_bracket2 = '\0';
         
         char *token = strtok(bracket1 + 1, ",");
         while (token != NULL && config->node_counter < MAX_NODES){
             // printf("tokens %s\n", token);
             config -> nodes[config->node_counter++] = atoi(token);
             token = strtok(NULL, ",]");
         }
 
         token = strtok(bracket2 + 1, ",]");
         while (token != NULL && config->faulty_counter < MAX_NODES) {
             // printf("tokens %s\n", token);
             config -> faulty_nodes[config->faulty_counter++] = atoi(token);
             token = strtok(NULL, ",");
         }
     }
     fclose(fp);
 }
 
 void checkNodeCount(int world_size, CSVRead* config){
     if (world_size != config -> node_counter){
         printf("Number of MPI processes, %d\n does not match number of nodes %d\n", world_size, config -> node_counter);
         MPI_Abort(MPI_COMM_WORLD, 1);
         exit(1);
     }
 }
 
 void printSummary(hStuff *hs, const TermSummary *summary, CSVRead* config)
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
         for (int i = 0; i < config->faulty_counter; i++)
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
 
 void initializeHotGrapez(hStuff *hs, int nodeId, int numNodes, TermSummary *summary, CSVRead* config)
 {
     hs->viewNum = 0;
     hs->nodeID = nodeId;
 
     // Set the first leader
     hs->leaderNodeID = 0;
     hs->state = (hs->nodeID == 0);
 
     bool is_faulty = false;
     for (int i = 0; i < config->faulty_counter; i++){
         if (nodeId == config->faulty_nodes[i]){
             is_faulty = true;
             break;
         }
     }
 
     hs->isFaulty = is_faulty;
     if (is_faulty) {
         summary->lyingNodes[summary->lyingNodeCount++] = hs->nodeID;
     }


     if (nodeId == 1) {
        hs->isFaulty = true;
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
     if (!(hs->state) && hs->nodeID != 1)
     {
         // Follower logic
         int receivedTerm;
         double start_time = MPI_Wtime();
         double timeout = 3.0; // 3 seconds timeout for receiving term
         
         MPI_Status status;
         MPI_Recv(&receivedTerm, 1, MPI_INT, hs->leaderNodeID, 0, MPI_COMM_WORLD, &status);
         
         // Check if the response is received within the timeout
         if (MPI_Wtime() - start_time < timeout)
         {
             write_log(hs->nodeID, "Node %d: Received new term %d from Leader. \n", hs->nodeID, term);
             write_log2("Node %d: Received new term %d from Leader. \n", hs->nodeID, term);
 
             if (receivedTerm >= hs->viewNum)
             {
                 hs->viewNum = receivedTerm; // update view
                 MPI_Send(&receivedTerm, 1, MPI_INT, hs->leaderNodeID, 1, MPI_COMM_WORLD);
             }
         }
         else
         {
             write_log(hs->nodeID, "Node %d: Timeout while waiting for new term from Leader. Ignoring response.\n", hs->nodeID);
         }
     }
     if (hs->nodeID == 1) {
         printf("[Node 1] remaining unresponsive during the start of the new term \n");
     }
 }
  
 void prepare_leader(hStuff *hs, int term, int world_size, TermSummary *summary, CSVRead *config)
 {
     int received_terms[world_size];
     int quorum = config->node_counter - ((config->node_counter - 1 + 2) / 3); // n - f (quorum size)
     int ackCount = 0; // acknowledgment count
     double timeout = 3.0; // 3 seconds timeout for receiving acknowledgments
     
     if (hs->state)
     {
         // Leader waits for acknowledgments
         write_log(hs->nodeID, "Leader %d: Waiting for followers to acknowledge new term %d. \n", hs->nodeID, term);
         write_log2("Leader %d: Waiting for followers to acknowledge new term %d. \n", hs->nodeID, term);
         
         MPI_Request request;
         MPI_Status status;
         
         for (int i = 0; i < world_size - 1; i++) {
             double startTime = MPI_Wtime();
             int ack;
             int flag = 0;
             MPI_Irecv(&ack, 1, MPI_INT, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &request);
             
             // Check for acknowledgment
             while (1) {
                 MPI_Test(&request, &flag, &status);
                 if (flag) {
                     if (ack == term) {
                         ackCount++;
                     }
                     break;
                 }
                 
                 // Check if timeout has been exceeded
                 if ((MPI_Wtime() - startTime) > timeout) {
                     printf("Timeout during PREPARE_LEADER. No response received. \n");
                     MPI_Cancel(&request);
                     MPI_Request_free(&request);
                     break;
                 }
             }
         } // end for
         
         printf("We got acknowledgements from %d nodes during the PREPARE phase. \n", ackCount);
         if (ackCount >= quorum)
         {
             write_log(hs->nodeID, "Leader %d: Quorum reached. Preparing a PREPARE message for term %d. \n", hs->nodeID, term);
             write_log2("Leader %d: Quorum reached. Preparing a PREPARE message for term %d. \n", hs->nodeID, term);
             summary->prepareQuorumReached = true;
             
             Message prepareMsg;
             QC qc = {term, hs};
             Msg(&prepareMsg, 42, term, &qc); // example: proposedVal = 42
             
             // Broadcast the prepare message
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
         }
     }
 }
  
 void prepare_follower(hStuff *hs, int term, int world_size, TermSummary *summary, CSVRead *config)
 {
     if (!(hs->state) && hs->nodeID != 1)
     {
         // Follower logic
         Message prepareMsg;
         double start_time = MPI_Wtime();
         double timeout = 3.0; // 3 seconds timeout for receiving prepare message
         
         MPI_Status status;
         MPI_Recv(&prepareMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 2, MPI_COMM_WORLD, &status);
         
         // Check if the message is received within the timeout
         if (MPI_Wtime() - start_time < timeout)
         {
             if (prepareMsg.viewNumber == term) 
             {
                 Message faultyMsg = prepareMsg;
                 faultyMsg.nodeID = hs->nodeID;
                 prepareMsg.nodeID = hs->nodeID;
                 if (hs->isFaulty == true)
                 {
                     write_log(hs->nodeID, "Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                     write_log2("Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                     faultyMsg.proposedVal = 2;
                     MPI_Send(&faultyMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 3, MPI_COMM_WORLD);
                 }
                 else
                 {
                     write_log(hs->nodeID, "Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                     write_log2("Node %d: Received PREPARE message for term %d. Sending PREPARE vote for term %d. \n", hs->nodeID, term);
                     MPI_Send(&prepareMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 3, MPI_COMM_WORLD);
                 }
             }
         }
         else
         {
             write_log(hs->nodeID, "Node %d: Timeout while waiting for PREPARE message from Leader. Ignoring response.\n", hs->nodeID);
         }
     }
     if (hs->nodeID == 1) {
         printf("[Node 1] is being unresponsive during PREPARE \n");
     }
 }
  
 void precommit_leader(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config)
 {
    int quorum = config->node_counter - ((config->node_counter - 1 + 2) / 3);
    int voteCount = 0;
    double timeout = 3.0;

     if (hs->state)
     {
         write_log(hs->nodeID, "Leader %d: Waiting for PREPARE votes for term %d. \n", hs->nodeID, term);
         write_log2("Leader %d: Waiting for PREPARE votes for term %d. \n", hs->nodeID, term);
         // printf("Leader %d: Waiting for PREPARE votes for term %d.\n", hs->nodeID, term);
         summary->precommitQuorumReached = true;

         MPI_Request request;
         MPI_Status status;
         
         for (int i = 0; i < world_size - 1; i++) {
            int flag = 0;
            double startTime = MPI_Wtime();

            Message receivedNote;
            MPI_Irecv(&receivedNote, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, 3, MPI_COMM_WORLD, &request);
            while (1) {
                MPI_Test(&request, &flag, &status);
                if (flag) {
                    if (receivedNote.viewNumber == term) {
                        voteCount++;
                    }
                    break;
                }

                if ((MPI_Wtime() - startTime) > timeout) {
                    printf("Timeout during PRE-COMMIT_LEADER. No response received \n");
                    MPI_Cancel(&request);
                    MPI_Request_free(&request);
                    break;
                }
            }
           
        }
         printf("We got vote counts from %d nodes during the PRE-COMMIT phase. \n", voteCount);
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
     if (!(hs->state) && hs->nodeID != 1)
     {
         Message precommitMsg;
         double startTime = MPI_Wtime();
         double timeout = 3.0;

         MPI_Status status;
         MPI_Recv(&precommitMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 4, MPI_COMM_WORLD, &status);
 
        if (MPI_Wtime() - startTime < timeout) {
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
     if (hs->nodeID == 1) {
        printf("[Node 1] is being unresponsive during PRE-COMMIT \n");
     }
 }
 
 void commit_leader(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config)
 {
    int quorum = config->node_counter - ((config->node_counter - 1 + 2) / 3);
    int voteCount = 0;

    double timeout = 3.0;

     if (hs->state)
     { // Leader logic
         write_log(hs->nodeID, "Leader %d: Waiting for PRE-COMMIT votes for term %d. \n", hs->nodeID, term);
         write_log2("Leader %d: Waiting for PRE-COMMIT votes for term %d. \n", hs->nodeID, term);
         // printf("Leader %d: Waiting for PRE-COMMIT votes for term %d.\n", hs->nodeID, term);
 
         MPI_Request request;
         MPI_Status status;

         for (int i = 0; i < world_size - 1; i++)
         {
            double startTime = MPI_Wtime();
            int flag = 0;
             Message receivedVote;
             MPI_Irecv(&receivedVote, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, 5, MPI_COMM_WORLD, &request);
 
             while(1) {
                MPI_Test(&request, &flag, &status);
                if (flag) {
                    if (receivedVote.viewNumber == term) {
                        voteCount++;
                    }
                } break;

                if ((MPI_Wtime() - startTime) > timeout) {
                    printf("Timeout during COMMIT_LEADER. No response received. \n");
                    MPI_Cancel(&request);
                    MPI_Request_free(&request);
                    break;
                }
             }
         }
 
         printf("We got vote counts from %d nodes during the COMMIT phase. \n", voteCount);
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
     if (!(hs->state) && hs->nodeID != 1)
     {
         Message commitMsg;
         double startTime = MPI_Wtime();
         double timeout = 3.0;

         MPI_Status status;
         MPI_Recv(&commitMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 6, MPI_COMM_WORLD, &status);
 
         if (MPI_Wtime() - startTime < timeout) {
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
     if (hs->nodeID == 1) {
        printf("[Node 1] is being unresponsive during COMMIT \n");
     }
 }
 
 void decide_leader(hStuff *hs, int term, int proposedVal, int world_size, TermSummary *summary, CSVRead *config)
 {
     double timeout = 3.0;

     if (hs->state)
     {
         write_log(hs->nodeID, "Leader %d: Waiting for COMMIT votes for term %d. \n", hs->nodeID, term);
         write_log2("Leader %d: Waiting for COMMIT votes for term %d. \n", hs->nodeID, term);
         // printf("Leader %d: Waiting for COMMIT votes for term %d.\n", hs->nodeID, term);
         int quorum = config->node_counter - ((config->node_counter - 1 + 2) / 3);
         int commitCount = 0;
         int count_42 = 0;
         int count_2 = 0;
         
         // Consider the leader's own vote first
         if (hs->isFaulty) {
             count_2++;
         } else  {
             count_42++;
         }
 
         MPI_Request request;
         MPI_Status status;
         for (int i = 0; i < world_size - 1; i++)
         {
             Message receivedCommit;
             double startTime = MPI_Wtime();
             int flag = 0;
             MPI_Irecv(&receivedCommit, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, 7, MPI_COMM_WORLD, &request);
 
             // printf("Leader: received commit vote from node %d and proposed value %d.\n", receivedCommit.nodeID, receivedCommit.proposedVal);
             while (1) {
                MPI_Test(&request, &flag, &status);
                if (flag) {
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
                    break;
                } 

                if ((MPI_Wtime() - startTime) > timeout) {
                    printf("Timeout during DECIDE_LEADER. No response received.\n");
                    MPI_Cancel(&request);
                    MPI_Request_free(&request);
                    break;
                }

             }

         }
         printf("We got a commit count of %d nodes during the DECIDE phase. \n", commitCount);

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
             //printf("count 42 is %d while count 2 is %d \n", count_42, count_2);
 
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
     if (!(hs->state) && hs->nodeID != 1)
     {
         Message decideMsg;
         double startTime = MPI_Wtime();
         double timeout = 3.0;

         MPI_Status status;
         MPI_Recv(&decideMsg, sizeof(Message), MPI_BYTE, hs->leaderNodeID, 8, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
 
         if (MPI_Wtime() - startTime < timeout) {
            if (decideMsg.viewNumber == term)
            {
                hs->value = decideMsg.proposedVal;
                write_log(hs->nodeID, "Node %d: DECISION made for term %d: value = %d.. \n", hs->nodeID, term, hs->value);
                write_log2("Node %d: DECISION made for term %d: value = %d.. \n", hs->nodeID, term, hs->value);
                //printf("Node %d: DECISION made for term %d: value = %d.\n", hs->nodeID, term, hs->value);
    
                summary->finalVal = hs->value;
                summary->leaderID = hs->leaderNodeID;
                summary->term = term;
            }
         }

         if (hs->nodeID == 1) {
            printf("[Node 1] is being unresponsive during DECIDE \n");
        }
   
     }
 }
 
 void rotateLeader(hStuff *hs, int world_size, CSVRead *config)
 {
     hs->leaderNodeID = (hs->leaderNodeID + 1) % config->node_counter; // round-robin leader rotation
     if (hs->leaderNodeID == 1) {
        hs->leaderNodeID = hs->leaderNodeID + 1;
     }
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
     MPI_Init(&argc, &argv);
     // int byzantine_fault_num = BYZANTINE_FAULTS;
 
     int rank, world_size;
     MPI_Comm_rank(MPI_COMM_WORLD, &rank);
     MPI_Comm_size(MPI_COMM_WORLD, &world_size);
 
     hStuff hs;
     CSVRead config;
 
     int term = 1;
 
     TermSummary summary;
 
     read_csv(&hs, &config);
     if(rank == 0){
         checkNodeCount(world_size, &config);
     }
 
     MPI_Barrier(MPI_COMM_WORLD);
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
 
         precommit_leader(&hs, term, 42, world_size, &summary, &config);
         MPI_Barrier(MPI_COMM_WORLD);
         precommit_follower(&hs, term, 42, world_size, &summary, &config);
         MPI_Barrier(MPI_COMM_WORLD);
 
         commit_leader(&hs, term, 42, world_size, &summary, &config); // Example proposed value = 42
         MPI_Barrier(MPI_COMM_WORLD);
         commit_follower(&hs, term, 42, world_size, &summary, &config); // Example proposed value = 42
         MPI_Barrier(MPI_COMM_WORLD);
 
         decide_leader(&hs, term, 42, world_size, &summary, &config);
         MPI_Barrier(MPI_COMM_WORLD);
         decide_follower(&hs, term, 42, world_size, &summary, &config);
         MPI_Barrier(MPI_COMM_WORLD);
 
         rotateLeader(&hs, world_size, &config); // Rotate leader after each term
         MPI_Barrier(MPI_COMM_WORLD);
 
         printSummary(&hs, &summary, &config); // mpi barrier
         MPI_Barrier(MPI_COMM_WORLD);
 
         term++;
     }
 
     MPI_Finalize();
     return 0;
 }


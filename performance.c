#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

void log_memory_usage(FILE *file) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {  // Track self usage instead of children
        fprintf(file, "Memory Usage: %ld KB\n", usage.ru_maxrss);
        fprintf(file,"\n");
    } else {
        perror("Error getting memory usage");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <c_program.c> <num_processes>\n", argv[0]);
        return 1;
    }

    char *source_file = argv[1];
    int num_processes = atoi(argv[2]); 
    char executable[100];
    snprintf(executable, sizeof(executable), "./mpi_program"); 

    char compile_cmd[200];
    snprintf(compile_cmd, sizeof(compile_cmd), "mpicc %s -o mpi_program", source_file);
    if (system(compile_cmd) != 0) {
        printf("MPI Compilation failed.\n");
        return 1;
    }

    FILE *logFile = fopen("performance_log.txt", "a");
    if (!logFile) {
        perror("Error opening file");
        return 1;
    }

    fprintf(logFile, "Executed MPI Program: %s\n", source_file);
    fprintf(logFile, "Number of MPI Processes: %d\n", num_processes);

    struct timeval start, end;
    gettimeofday(&start, NULL);

    char run_cmd[200];
    snprintf(run_cmd, sizeof(run_cmd), "mpirun -np %d %s > mpi_output.txt 2>&1", num_processes, executable);
    int status = system(run_cmd);

    gettimeofday(&end, NULL);
    double elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0 + 
                          (end.tv_usec - start.tv_usec) / 1000.0;
    
    fprintf(logFile, "Execution Time: %.2f ms\n", elapsed_time);
    log_memory_usage(logFile);

    // fflush(logFile);
    fclose(logFile);

    printf("Performance logged to performance_log.txt\n");

    return status;
}

// INSTRUCTIONS:
// 1:  gcc performance.c -o performance
// 2:  ./performance <MPI_C_Program.c> <num_processes>
//     EX:
//         ./performance hotstuff_regular.c 3
// 3: sometimes you may have to wait a few seconds then ^C (control c)
//       you should get a response written in terminal that says:
//          "Performance logged to performance_log.txt"
// 4: open performance_log.txt
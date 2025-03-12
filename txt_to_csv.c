#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LENGTH 512

void convert_to_csv(FILE *input, FILE *output) {
    char line[MAX_LINE_LENGTH];
    char mpi_program[100];
    int num_processes;
    double exec_time;
    int memory_usage;
    
    fprintf(output, "Executed MPI Program,Number of MPI Processes,Execution Time (ms),Memory Usage (KB)\n");
    while (fgets(line, sizeof(line), input)) {

        if (strstr(line, "Executed MPI Program:") != NULL) {
            sscanf(line, "Executed MPI Program: %s", mpi_program);

        } else if (strstr(line, "Number of MPI Processes:") != NULL) {
            sscanf(line, "Number of MPI Processes: %d", &num_processes);

        } else if (strstr(line, "Execution Time:") != NULL) {
            sscanf(line, "Execution Time: %lf ms", &exec_time);

        } else if (strstr(line, "Memory Usage:") != NULL) {
            sscanf(line, "Memory Usage: %d KB", &memory_usage);
            
            fprintf(output, "%s,%d,%.2lf,%d\n", mpi_program, num_processes, exec_time, memory_usage);
        }
    }
}

int main() {
    FILE *input_file = fopen("performance_log.txt", "r");
    if (input_file == NULL) {
        fprintf(stderr, "Error opening input file.\n");
        return 1;
    }

    FILE *output_file = fopen("performance.csv", "a");
    if (output_file == NULL) {
        fprintf(stderr, "Error opening output file.\n");
        fclose(input_file);
        return 1;
    }

    convert_to_csv(input_file, output_file);

    fclose(input_file);
    fclose(output_file);

    printf("Conversion completed successfully.\n");
    return 0;
}

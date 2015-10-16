#pragma once
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_NR_COUNTERS 64

FILE * logFile = NULL;

typedef pthread_mutex_t Mutex;

typedef struct {
	int number;
	time_t open_time;
	int time_opened;
	char fifo_name [50];
	int clients_attended;
	int clients_counter;
	float avg;
	Mutex mutex;
} Counter;

typedef struct {
	int totalCounters;
	int openCounters;
	time_t storeOpenningTime;
	Counter counters[MAX_NR_COUNTERS];
} Shared_memory;

typedef struct {
	char client_fifo [50];
	int counter_number;
} New_client;
//function to read from a file until null is found
int readLine(int fd, char * buffer){
        char character[1];
        int i = 0;
        while(read(fd,character,1) > 0){
                buffer[i] = character[0];
                i++;
                if(character[0] == '\0'){
                    return i;
                }
        }
        return i;
}
//log information about counters and clients
void log_line(char * quem, int balcao, char * oque, char * canal){
	char date[100];
	time_t now = time(NULL);
	struct tm *t = localtime(&now);


	strftime(date, sizeof(date), "%d-%m-%Y %H:%M:%S", t);
	fprintf(logFile, "%-20s", date);
	fprintf(logFile, "| %-8s", quem);
	fprintf(logFile, "| %-9d", balcao);
	fprintf(logFile, "| %-20s", oque);
	fprintf(logFile, "| %-14s\n", canal);
	fflush(logFile);
}
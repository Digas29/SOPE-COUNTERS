#include "balcao.h"
#include <semaphore.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>

sem_t * sem;


int main(int argc, char * argv[]){

	if(argc != 3){
		printf("Usage: ger_cl <shared_memory_name> <nr_clients>");
		exit(1);
	}

	int nr_clients = atoi(argv[2]);

	Shared_memory * shm;

	//Preparar nomes

	char shm_name [50] = "/";
	strcat(shm_name, argv[1]);

	char sem_name [50] = "/sem_";
	strcat(sem_name, argv[1]);

	char log_name[50];
	strcpy(log_name, argv[1]);
	strcat(log_name, ".log");

	//abrir semaforo, memoria partilhada (e respectivo mapeamento) e ficheiro de log
	sem = sem_open(sem_name, O_CREAT, 0777, 0);
	if(sem == SEM_FAILED){
		perror("Failure in sem_open()");
		exit(1);
	}

	int shm_fd = shm_open(shm_name, O_RDWR, 0777);
	if(shm_fd < 0){
		perror("Failure in shm_open()");
 		exit(2);
	}

	shm = (Shared_memory *) mmap(0, sizeof(Shared_memory), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if(shm == (void *)-1){
		perror("Failure in mmap()");
		exit(3);
	}
	int logfd = open(log_name, O_WRONLY | O_APPEND, 0777);
	if(logfd < 0){
		perror("Failure in open() log file");
 		exit(5);
	}
	logFile = fdopen(logfd,"a");

	//gerar um processo por cliente
	int i;
	for(i = 0; i < nr_clients; i++){
		int pid = fork();
		if(pid == 0){
			alarm(50); //time_out para arranjar um balcao -> 50 segundos

			//criar nome do fifo do cliente
			char client_fifo[50] = "/tmp/fc_";

			char buffer[1024];
			sprintf(buffer, "%d", getpid());
			strcat(client_fifo, buffer);

			int balcao = -1;

			//criar e abrir fifo de cliente
			if(mkfifo(client_fifo, 0777) < 0){
				perror("Failure in mkfifo()");
				exit(4);
			}

			int client_fifo_fd = open(client_fifo, O_RDWR);
			if(client_fifo_fd < 0){
				perror("Failure in open() fifo");
				exit(5);
			}
			
			int j;
			int min = 1024;

			//percorrer todos os balcoes
			sem_wait(sem);
			
			for(j = 0; j < shm->totalCounters; j++){
				pthread_mutex_lock(&(shm->counters[j].mutex));
				if(shm->counters[j].time_opened == -1 && shm->counters[j].clients_counter < min){
					min = shm->counters[j].clients_counter;
					balcao = j;
				}
				pthread_mutex_unlock(&(shm->counters[j].mutex));
			}

			sem_post(sem);
			//enviar informacao para o balcao
			if(balcao != -1){
				log_line("Cliente", balcao + 1, "pede_atendimento", client_fifo);
				int counter_fifo_fd = open(shm->counters[balcao].fifo_name, O_WRONLY);
				if(counter_fifo_fd < 0){
					perror("Failure in open() counter fifo");
					exit(6);
				}
				write(counter_fifo_fd, client_fifo, (strlen(client_fifo) + 1)* sizeof(char));
				close(counter_fifo_fd);
				char info[1024];
				readLine(client_fifo_fd, info);
				log_line("Cliente", balcao + 1, "fim_atendimento", client_fifo);
			}
			close(client_fifo_fd);
			unlink(client_fifo);
			break;

		}
	}
	//fechar ficheiros e desmapear memoria
	fclose(logFile);
	sem_close(sem);
	close(shm_fd);
	munmap(shm, sizeof(Shared_memory));
	return 0;
}

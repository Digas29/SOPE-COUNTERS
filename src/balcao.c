#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <semaphore.h>
#include "balcao.h"


sem_t  * sem;
pthread_cond_t allServed = PTHREAD_COND_INITIALIZER; //variavel de condicao para esperar que todos os clientes sejam atendidos
Shared_memory * shm;

/*  
 * escrever no fifo do balcao qunado tempo se esgotar
*/
void * notifyWhenDone(void * args){
	int  * parameter = (int *) args;
	sleep(parameter[0]);
	write(parameter[1], "done\0", 5 * sizeof(char));
	return NULL;
}
/*  
 * escrever cabecalho no ficheiro de log
*/
void prepare_log_file(){
	fprintf(logFile, "%-20s", "quando");
	fprintf(logFile, "| %-8s", " quem");
	fprintf(logFile, "| %-9s", " balcao");
	fprintf(logFile, "| %-20s", " o_que");
	fprintf(logFile, "| %-14s\n", " canal_criado/usado");
	fprintf(logFile, "%s\n", "--------------------------------------------------------------------------------------");
	fflush(logFile);
}
/*  
 * thread de atendimento
*/
void * serveClient(void * args){
	New_client * clientP = (New_client *) args;

	//copiar argumentos
	New_client client;
	client.counter_number = clientP->counter_number;
	strcpy(client.client_fifo, clientP->client_fifo);

	//calculo de tempo de atendimento e nr de clientes no balcao
	pthread_mutex_lock(&(shm->counters[client.counter_number - 1].mutex));
	log_line("Balcao", client.counter_number, "inicia_atend_cli", client.client_fifo);
	shm->counters[client.counter_number - 1].clients_counter++;
	int sleepTime = shm->counters[client.counter_number - 1].clients_counter;
	pthread_mutex_unlock(&(shm->counters[client.counter_number - 1].mutex));

	if(sleepTime > 10){
		sleepTime = 10;
	}
	sleep(sleepTime);

	//escrever no fifo do cliente
	int client_fd = open(client.client_fifo, O_RDONLY);
	if(client_fd == - 1){
		perror("Failure in opening client fifo");
		return NULL;
	}
	write(client_fd, "done", 5 * sizeof(char));

	//calcular media e atualizar nr de clientes no balcao e verificam de situacao de fecho de balcao
	pthread_mutex_lock(&(shm->counters[client.counter_number - 1].mutex));
	shm->counters[client.counter_number - 1].clients_counter--;
	float total_time = (float)shm->counters[client.counter_number - 1].clients_attended * shm->counters[client.counter_number - 1].avg + (float)sleepTime;
	shm->counters[client.counter_number - 1].clients_attended++;
	int total_clients = shm->counters[client.counter_number - 1].clients_attended;
	shm->counters[client.counter_number - 1].avg = total_time/(float)total_clients;

	if(shm->counters[client.counter_number - 1].time_opened != -1 && 
		shm->counters[client.counter_number - 1].clients_counter == 0)
		pthread_cond_signal(&allServed);

	log_line("Balcao", client.counter_number, "fim_atend_cli", client.client_fifo);
	pthread_mutex_unlock(&(shm->counters[client.counter_number - 1].mutex));
	
	close(client_fd);
	return NULL;
}

int main(int argc, char * argv[]){

	if(argc != 3){
		printf("Usage: balcao <shared_name> <open_time>");
		exit(1);
	}

	//preparar nomes
	char shm_name [50] = "/";
	strcat(shm_name, argv[1]);

	char sem_name [50] = "/sem_";
	strcat(sem_name, argv[1]);

	char log_name[50];
	strcpy(log_name, argv[1]);
	strcat(log_name, ".log");

	//abrir semaforo de variaveis globais
	sem = sem_open(sem_name, O_CREAT, 0777, 0);
	if(sem == SEM_FAILED){
		perror("Failure in sem_open() create");
		exit(5);
	}
	int initalize = 0;

	//preparar memoria partilhada. Ao abrir da erro se existir => util para inicializacao e reserva de espaco
	int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR | O_EXCL, 0777);

	if(shm_fd < 0){
		if(errno != EEXIST){
			perror("Failure in shm_open()");
 			exit(2);
		}
		else{
			shm_fd = shm_open(shm_name, O_RDWR , 0777);
		}
	}
	else{ 
		if (ftruncate(shm_fd, sizeof(Shared_memory)) < 0){
			perror("Failure in ftruncate()");
 			exit(3);
 		}
 		else{
 			initalize = 1;

 		}
	}
	//mapear memoria partilhada
	shm = (Shared_memory *) mmap(0, sizeof(Shared_memory), PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if(shm == (void *)-1){
		perror("Failure in mmap()");
		exit(4);
	}

	int logfd;

	//prepar ficheiros se necessario ou ler estado actual da memoria;
	if(initalize){
		logfd = open(log_name, O_WRONLY | O_CREAT | O_TRUNC, 0777);
		logFile = fdopen(logfd,"a");
		prepare_log_file();
		log_line("Balcao", 1, "incia_mempart", "-");
		Shared_memory global;
		global.totalCounters = 1;
		global.openCounters = 1;
		global.storeOpenningTime = time(NULL);
		*shm = global;
	}
	else{
		logfd = open(log_name, O_WRONLY | O_APPEND, 0777);
		logFile = fdopen(logfd,"a");
		sem_wait(sem);
		if(shm->totalCounters == MAX_NR_COUNTERS){
			printf("Maximum number of counters reached! \n");
			exit(4);
		}
		shm->totalCounters++;
		shm->openCounters++;
	}
	if(logfd < 0){
		perror("Failure in open() log file");
		exit(7);
	}

	// Preparar balcao
	Counter counter;

	counter.number = shm->totalCounters;

	sem_post(sem);
	//mutex disponivel noutros processos
	pthread_mutexattr_t mattr;
	if(pthread_mutexattr_init(&mattr) != 0 || pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED) != 0){
		perror("Failure in pthread_mutexattr_setpshared()");
		exit(9);
	}

	counter.open_time = time(NULL);
	counter.time_opened = -1;
	counter.avg = 0.0;
	strcpy(counter.fifo_name, "/tmp/fb_");
	char buffer[1024];
	sprintf(buffer, "%d", getpid());
	strcat(counter.fifo_name, buffer);
	counter.clients_attended = 0;
	counter.clients_counter = 0;
	
	//inicializar mutex e partilhar informacao do balcao
	sem_wait(sem);

	shm->counters[shm->totalCounters - 1] = counter;
	if(pthread_mutex_init(&(shm->counters[counter.number - 1].mutex), &mattr) != 0){
		perror("Failure in pthread_mutex_init()");
		exit(10);
	}
	sem_post(sem);

	log_line("Balcao", counter.number, "cria_linh_mempart", counter.fifo_name);


	//criar fifo do balcao
	if(mkfifo(counter.fifo_name, 0777) < 0){
		perror("Failure in mkfifo()");
		exit(5);
	}

	int fifo_fd = open(counter.fifo_name, O_RDWR);
	if(fifo_fd < 0){
		perror("Failure in open() fifo");
		exit(6);
	}

	pthread_t tid;
	int args[2];
	args[0] = atoi(argv[2]);
	args[1] = fifo_fd;

	//criar thread para notificar quando tempo se esgotar
	pthread_create(&tid, NULL, notifyWhenDone, args);
	pthread_detach(tid);

	//esperar que chegue alguma informacao ao fifo do balcao
	strcpy(buffer,"");
	readLine(fifo_fd, buffer);

	while(strcmp(buffer, "done")){
		New_client client;
		pthread_t ctid = 0;
		client.counter_number = counter.number;
		strncpy(client.client_fifo, buffer, 50);
		pthread_create(&ctid, NULL, serveClient, &client);
		pthread_detach(ctid);
		strcpy(buffer,"");
		readLine(fifo_fd, buffer);
	};
	unlink(counter.fifo_name);

	//esperar por todos os processos
	pthread_mutex_lock(&(shm->counters[counter.number - 1].mutex));
	shm->counters[counter.number - 1].time_opened = atoi(argv[2]);
	while(shm->counters[counter.number - 1].clients_counter != 0){
		pthread_cond_wait(&allServed, &(shm->counters[counter.number - 1].mutex));
	}
	pthread_mutex_unlock(&(shm->counters[counter.number - 1].mutex));

	log_line("Balcao", counter.number, "fecha_balcao", counter.fifo_name);
	
	//verificar se loja vai fechar e imprimir estatisticas
	sem_wait(sem);
	shm->openCounters--;
	if(shm->openCounters == 0){
		int i;
		printf("\n\nSTATISTICS %s \n \n", argv[1]);
		for(i = 0; i < shm->totalCounters; i++){
			printf("#%d COUNTER \n\n", i + 1);
			printf("Time opened: %d \n", shm->counters[i].time_opened);
			printf("Clients served: %d \n", shm->counters[i].clients_attended);
			printf("Average time per client: %f \n\n", shm->counters[i].avg);
		}
		//fechar os ficheiros e apagar todas as informacoes
		sem_post(sem);
		sem_close(sem);
		sem_unlink(sem_name);
		close(shm_fd);
		shm_unlink(shm_name);
		munmap(shm, sizeof(Shared_memory));
		log_line("Balcao", counter.number, "fecha_loja", counter.fifo_name);
		fclose(logFile);
		return 0;
	}
	//fechar os ficeiros e desmapear a memoria
	sem_post(sem);
	fclose(logFile);
	sem_close(sem);
	close(shm_fd);
	munmap(shm, sizeof(Shared_memory));
	return 0;
}

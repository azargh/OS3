#include "segel.h"
#include "request.h"
#include "server.h"
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sched.h>
#include <assert.h>

// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// HW3: Parse the new arguments too

pthread_cond_t not_empty;
pthread_cond_t not_full;
pthread_mutex_t lock;
struct Thread** thread_array;
struct RingBuffer requests;
int num_of_threads;

enum _schedalg{
	BLOCK,
	DT,
	DH,
	RANDOM};
	
	
struct RingBuffer{
	struct Request** array;
	int consumer_idx, producer_idx;
	int size;
	int tasks_in_progress;
	int max_size;
	enum _schedalg alg;
};
	
void add_to_ringbuffer(int val)
{
	//printf("add_to_ringbuffer: started\n");
	//printf("add_to_ringbuffer: at start, producer idx is %d\n", buffer->producer_idx);
	//printf("add_to_ringbuffer: at start, consumer idx is %d\n", buffer->consumer_idx);
	pthread_mutex_lock(&lock);
	struct timeval time;
	gettimeofday(&time, NULL);
	//printf("size is: %d\n", buffer->size);
	//printf("producer idx: %d\n", buffer->producer_idx);
	//printf("consumer idx: %d\n", buffer->consumer_idx);
	//printf("val is: %d\n", val);
	if (requests.size + requests.tasks_in_progress == requests.max_size)
	{
		if (requests.alg == BLOCK)
		{
			//printf("entered the blocking if\n");
			//assert(buffer->producer_idx == buffer->consumer_idx);
			while(requests.size + requests.tasks_in_progress == requests.max_size)
			{
				//printf("waiting on not full signal\n");
				pthread_cond_wait(&not_full, &lock);
				//printf("got the not full signal\n");
			}
			gettimeofday(&time, NULL);
			requests.array[requests.producer_idx]->fd = val;
			requests.array[requests.producer_idx]->arrival = time;
			requests.size++;
			requests.producer_idx++;
			if(requests.producer_idx == requests.max_size)
				requests.producer_idx = 0;
			printf("sent not empty signal in alg == block scope\n");
			pthread_cond_signal(&not_empty);  
			pthread_mutex_unlock(&lock);
			return;
			
		}
		else if (requests.alg == DT)
		{
			Close(val);
			printf("sent not empty signal in alg == dt scope\n");
			pthread_cond_signal(&not_empty);
			pthread_mutex_unlock(&lock);
			return;
		}
		else if (requests.alg == DH)
		{
			Close(requests.array[requests.consumer_idx]->fd);
			requests.consumer_idx++;
			requests.array[requests.producer_idx]->fd = val;
			requests.array[requests.producer_idx]->arrival = time;
			requests.producer_idx++;
			if(requests.producer_idx == requests.max_size)
				requests.producer_idx = 0;
			if(requests.consumer_idx == requests.max_size)
				requests.consumer_idx = 0;
			printf("sent not empty signal in alg == dh scope\n");
			pthread_cond_signal(&not_empty);
			pthread_mutex_unlock(&lock);
			return;
		}
		else if (requests.alg == RANDOM)
		{
			pthread_cond_signal(&not_empty);
			pthread_mutex_unlock(&lock);
			return; // todo: implement random
		}
	}
	else
	{
		//printf("entered the else\n");
		requests.array[requests.producer_idx]->fd = val;
		requests.array[requests.producer_idx]->arrival = time;
		requests.producer_idx++;
		requests.size++;
		if(requests.producer_idx == requests.max_size)
			requests.producer_idx = 0;
		printf("sent not empty signal in \"else\" scope\n");
		pthread_cond_signal(&not_empty);
		pthread_mutex_unlock(&lock);
		//printf("exitted the else\n");
		return;
	}
	printf("reached unexpected end of add_to_ringbuffer\n");
	//printf("add_to_ringbuffer: at end, producer idx is %d\n", buffer->producer_idx);
	//printf("add_to_ringbuffer: at end, consumer idx is %d\n", buffer->consumer_idx);
	//printf("add to ringbuffer: ended\n");
}

void* do_request_handle(void* _thread)
{
	struct Thread* thread = (struct Thread*) _thread;
	struct timeval time;
	printf("time address is: %p\n", &time);
	while(1)
	{
		gettimeofday(&time, NULL);
		printf("thread %d waiting for mutex at beginning of do_request_handle time: %lu.06%lu\n", thread->id, time.tv_sec, time.tv_usec);
		pthread_mutex_lock(&lock);
		gettimeofday(&time, NULL);
		printf("thread %d got mutex at beginning of do_request_handle time: %lu.06%lu\n", thread->id, time.tv_sec, time.tv_usec);
		while(requests.size == 0)
		{
			//printf("thread waiting on lock\n");
			pthread_cond_wait(&not_empty, &lock);
			//printf("thread waiting on lock after cond\n");
		}
			
		gettimeofday(&time, NULL);
		
		struct Request* request = requests.array[requests.consumer_idx];
		
		timersub(&time, &request->arrival, &request->dispatch);
		
		request->thread_info = thread;
		
		int old_index = requests.consumer_idx;
		
		requests.consumer_idx++;
		if(requests.consumer_idx == requests.max_size)
			requests.consumer_idx = 0;
		requests.size--;
		requests.tasks_in_progress++;
		gettimeofday(&time, NULL);
		printf("thread %d releasing mutex at before requestHandle in do_request_handle, time: %lu.06%lu\n", thread->id, time.tv_sec, time.tv_usec);
		pthread_mutex_unlock(&lock);
		
		requestHandle(*requests.array[old_index]);
		Close(requests.array[old_index]->fd);
		
		gettimeofday(&time, NULL);
		printf("thread %d waiting on mutex to update tasks in progress in do_request_handle, time: %lu.06%lu\n", thread->id, time.tv_sec, time.tv_usec);
		pthread_mutex_lock(&lock);
		gettimeofday(&time, NULL);
		printf("thread %d got mutex to update tasks in progress in do_request_handle, time: %lu.06%lu\n", thread->id, time.tv_sec, time.tv_usec);
		requests.tasks_in_progress--;
		pthread_cond_signal(&not_full);
		pthread_mutex_unlock(&lock);
		gettimeofday(&time, NULL);
		printf("thread %d releasing mutex after updating tasks in progress in do_request_handle, time: %lu.06%lu\n", thread->id, time.tv_sec, time.tv_usec);
	}
}

void getargs(int *port, int *_num_of_threads, int *queue_size, enum _schedalg *schedalg, int argc, char *argv[])
{
    if (argc < 4) {
	fprintf(stderr, "Usage: %s <port> <threads> <queue_size> <schedalg>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
    *_num_of_threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    if (strcmp(argv[4], "block") == 0)
		*schedalg = BLOCK;
	else if (strcmp(argv[4], "dt") == 0)
		*schedalg = DT;
	else if (strcmp(argv[4], "ht") == 0)
		*schedalg = DH;
	else if (strcmp(argv[4], "random") == 0)
		*schedalg = RANDOM;
}


int main(int argc, char *argv[])
{
    int listenfd, connfd, port, queue_size, clientlen;
    enum _schedalg schedalg;
    struct sockaddr_in clientaddr;
	pthread_mutex_init(&lock, NULL);
	thread_array = (struct Thread**) malloc(sizeof(struct Thread*) * num_of_threads);
	
    getargs(&port, &num_of_threads, &queue_size, &schedalg, argc, argv);
	 
	// initializing the ring buffer
	requests.array = (struct Request**) malloc(sizeof(struct Request*) * queue_size);
	requests.consumer_idx = 0;
	requests.producer_idx = 0;
	requests.size = 0;
	requests.max_size = queue_size;
	requests.tasks_in_progress = 0;

	for (int i=0; i < num_of_threads; ++i)  // creating threads
	{
		struct Thread* new_thread = (struct Thread*) malloc(sizeof(struct Thread));
		pthread_create(&new_thread->thread, NULL, do_request_handle, new_thread);
		new_thread->count = 0;
		new_thread->dynamic_count = 0;
		new_thread->static_count = 0;
		new_thread->id = i;
		thread_array[i] = new_thread;
	}
	
	for(int i=0; i < queue_size; ++i)
	{
		struct Request* new_request = (struct Request*) malloc(sizeof(struct Request));
		new_request->fd = -1;
		requests.array[i] = new_request;
	}
	
    listenfd = Open_listenfd(port);
    while (1) 
    {
		//printf("main thread: %lu\n", pthread_self());
		//printf("waiting on request\n");
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
		//printf("request acquired: fd is %d\n", connfd);
		add_to_ringbuffer(connfd);
    }
}


    


 

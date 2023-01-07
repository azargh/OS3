#ifndef __SERVER_H__

struct Thread{
	pthread_t thread;
	int id;
	int count;
	int static_count;
	int dynamic_count;
};

struct Request{
	int fd;
	struct timeval arrival;
	struct timeval dispatch;
	struct Thread* thread_info;
};

#endif

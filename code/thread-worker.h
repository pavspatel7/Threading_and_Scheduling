// File:	worker_t.h

// Group Member's: Pavitra Patel (php51), Kush Patel(kp1085)
// Username of iLab: rlab2.cs.rutgers.edu
// iLab Server: cs416f23-28

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>

// Global MACROS
#define timeQuantum 50000;        // time slice in ms
#define QUEUE_NUM 4               // For MLFQ

typedef uint worker_t;

typedef struct TCB
{
	worker_t id;
	int status;
	ucontext_t context;
	void* stack;
	void* returnValue;
	worker_t joinThreadID;
	struct timespec start_time;
	struct timespec end_turn_time;
	struct timespec end_response_time;
	int times_scheduled;
	int priority;          // priority also determines queue number of this thread
} tcb;

typedef struct ThreadNode {
	tcb *thread;
	struct ThreadNode* next;
} ThreadNode;

typedef struct Runqueue {
	ThreadNode* head;
	ThreadNode* tail;
} Runqueue;

/* mutex struct definition */
typedef struct worker_mutex_t
{
	tcb *holdingTCB;    // tcb of thread that holds the lock
	atomic_flag status;
	Runqueue *wq;       // tcb queue of waiting threads
} worker_mutex_t;



// enum that corresponds to status of the thread
typedef enum
{
	THREAD_CREATED,
	THREAD_READY,
	THREAD_RUNNING,
	THREAD_JOIN_BLOCKED,
	THREAD_EXITED,
	THREAD_MUTEX_BLOCK
} ThreadStatus;

/* Function Declarations for Queue Data Structure */
Runqueue* init_runqueue();
void enqueue(Runqueue *rq, tcb *thread);
void dequeue(Runqueue* rq, tcb* thread);
int isRunqueueEmpty(Runqueue *rq);
int runqueueSize(Runqueue *rq);
tcb* findTCBInRunqueue(Runqueue* rq, worker_t thread);
tcb* dequeueNext(Runqueue* rq);
void enqueueAtHead(Runqueue* rq, tcb* thread);
void sort(Runqueue* rq);
void freeRunqueue(Runqueue* rq);
void freeTCB(tcb* thread);



/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
    *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);

/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);



#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif

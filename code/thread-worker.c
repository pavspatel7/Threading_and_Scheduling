// File:	thread-worker.c

// Group Member's: Pavitra Patel (php51), Kush Patel(kp1085)
// Username of iLab: rlab2.cs.rutgers.edu
// iLab Server: cs416f23-28

// This command can be run from benchmarks folder
// cd .. && make clean && make SCHED=PSJF && cd benchmarks && make clean && make && ./parallel_cal 6


#include "thread-worker.h"
#include "ucontext.h"
#include "malloc.h"
#include "stdlib.h"
#include "signal.h"
#include "stdio.h"
#include "sys/time.h"
#include "string.h"
#include "time.h"


//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;


// Global MACROS
#define STACK_SIZE SIGSTKSZ
int thread_count = 1;               // 0 for main, user thread starts at 2
tcb *curr_run_tcb = NULL;           // global pointer to current running thread tcb
ucontext_t sched_context;           // global variable that saves scheduler context
Runqueue *terminated_tcbs;          // global queue for storing tcb's of terminated/exited threads
tcb *mtcb;                          // tcb for main thread
struct itimerval timer;             // timer global
Runqueue *blocked_join_tcbs = NULL; // tcb for THREAD_JOIN_BLOCK
double temp_avg_turn_time=0;
double temp_avg_resp_time=0;
enum schedulers {
	_PSJF,
	_MLFQ
};
int SCHED = _PSJF;
Runqueue **sched_rq;           // scheduler runqueue
int isYield = 0;               // For MLFQ: if 0 no yield, if 1 yield


/* function declarations */
static void schedule(void);
static void sched_psjf(void);
static void sched_mlfq(void);
static void sched_rr(int);
void print_app_stats(void);


/* timer interrupt signal handler */
static void signal_handle(int signum)
{
	getcontext(&curr_run_tcb->context);
	swapcontext(&curr_run_tcb->context, &sched_context);
}

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

		tcb *ntcb = (tcb*)malloc(sizeof(tcb));
		if(ntcb != NULL)
			thread_count++;
		else
			perror("Error creating TCB");

		ntcb -> id = thread_count;
		*thread = ntcb -> id;     // Assign the current thread ID to the thread pointer
		ntcb -> status = THREAD_CREATED;

		getcontext(&(ntcb -> context));
		ntcb -> stack = malloc(STACK_SIZE);
		ntcb -> context.uc_link = NULL;
		ntcb -> context.uc_stack.ss_sp = ntcb -> stack;
		ntcb -> context.uc_stack.ss_size = STACK_SIZE;
		ntcb -> context.uc_stack.ss_flags = 0;
		ntcb -> times_scheduled = 0;
		ntcb -> joinThreadID = -1;
		ntcb -> priority = 0;
		clock_gettime(CLOCK_REALTIME, &(ntcb->start_time));

		if(arg != NULL)
        	makecontext(&ntcb->context, (void*)function, 1, arg);
    	else
        	makecontext(&ntcb->context, (void*)function, 0);

		// enter's "if" only when worker_create is called for the first time
		if(thread_count == 2)
		{
			// init sched
			getcontext(&sched_context);
			sched_context.uc_link=NULL;
			sched_context.uc_stack.ss_sp=malloc(STACK_SIZE);
			sched_context.uc_stack.ss_size=STACK_SIZE;
			sched_context.uc_stack.ss_flags=0;
			makecontext(&sched_context, (void*)&schedule, 0);

			terminated_tcbs = init_runqueue();
			blocked_join_tcbs = init_runqueue();

			#ifndef MLFQ
				SCHED = _PSJF;
			#else 
				SCHED = _MLFQ;
			#endif

			if(SCHED == _PSJF)
			{
				sched_rq = malloc(1 * sizeof(Runqueue*));
				sched_rq[0] = init_runqueue();
			}
			else
			{
				sched_rq = malloc(QUEUE_NUM * sizeof(Runqueue*));
				for(int i=0; i<QUEUE_NUM; i++)
					sched_rq[i] = init_runqueue();
			}

			// init main tcb
			mtcb = (tcb*)malloc(sizeof(tcb));
			mtcb -> id = 0;
			mtcb -> status = THREAD_RUNNING;
			mtcb -> priority = 0;
			getcontext(&mtcb -> context);
			curr_run_tcb = mtcb;

			// create and start the timer
			struct sigaction sa;
			memset (&sa, 0, sizeof (sa));
			sa.sa_handler = &signal_handle;
			sigaction (SIGPROF, &sa, NULL);
			timer.it_interval.tv_sec = 0;
			timer.it_interval.tv_usec = timeQuantum;
			timer.it_value.tv_sec = 0;
			timer.it_value.tv_usec = timeQuantum;
			setitimer(ITIMER_PROF, &timer, NULL);
		}
		enqueue(sched_rq[ntcb->priority], ntcb);
    return 0;
};

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{
	isYield = 1;
	curr_run_tcb -> status = THREAD_READY;
	getcontext(&curr_run_tcb -> context);
	swapcontext(&curr_run_tcb -> context, &sched_context);
	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr)
{
	curr_run_tcb -> returnValue = value_ptr;
	curr_run_tcb -> status = THREAD_EXITED;
	
	// if other threads are waiting for this thread to exit, notify them they can proceed
	if(curr_run_tcb -> joinThreadID != -1)
	{
		worker_t releaseThreadID = curr_run_tcb -> joinThreadID;
		tcb *releaseThreadTCB = findTCBInRunqueue(blocked_join_tcbs, releaseThreadID);
		releaseThreadTCB -> status = THREAD_READY;
		dequeue(blocked_join_tcbs, releaseThreadTCB);
		enqueue(sched_rq[releaseThreadTCB->priority], releaseThreadTCB);
	}
	enqueue(terminated_tcbs, curr_run_tcb);  // add the exited thread tcb to terminated_tcbs 

	if(curr_run_tcb->id != 0)
	{
		clock_gettime(CLOCK_REALTIME, &(curr_run_tcb->end_turn_time));
		temp_avg_turn_time += (double) (curr_run_tcb->end_turn_time.tv_sec - curr_run_tcb->start_time.tv_sec) * 1000 + (curr_run_tcb->end_turn_time.tv_nsec - curr_run_tcb->start_time.tv_nsec) / 1000000;
		avg_turn_time = temp_avg_turn_time/(thread_count-1);
	}
	free(curr_run_tcb->context.uc_stack.ss_sp);
	setcontext(&sched_context);
};

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr)
{
	tcb *jtcb = NULL;
	int index = 0;
	if(SCHED == _PSJF)
		index = 1;
	else
		index = QUEUE_NUM;
	for(int i=0; i<index; i++)
	{
		if(jtcb == NULL)
			jtcb = findTCBInRunqueue(sched_rq[i], thread);
		else
			break;
	}
	if(jtcb == NULL)
	{
		tcb *etcb = findTCBInRunqueue(terminated_tcbs, thread);
		if(etcb != NULL)
		{
			// do nothing
		}
	}
	else if(jtcb -> status != THREAD_EXITED)
	{
		curr_run_tcb -> status = THREAD_JOIN_BLOCKED;
		jtcb -> joinThreadID = curr_run_tcb -> id;
		enqueue(blocked_join_tcbs, curr_run_tcb);
	}
	swapcontext(&curr_run_tcb->context, &sched_context);
	if(value_ptr != NULL)
		*value_ptr = jtcb -> returnValue;
	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
    static worker_mutex_t temp_mutex;
    temp_mutex.holdingTCB = NULL;
    atomic_flag_clear(&temp_mutex.status);
    temp_mutex.wq = init_runqueue();
	*mutex = temp_mutex;
    return 0;
}

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{
	while(atomic_flag_test_and_set(&mutex->status))
	{
		// reaches here when a thread doesn't get a lock
		enqueue(mutex->wq, curr_run_tcb);
		curr_run_tcb -> status = THREAD_MUTEX_BLOCK;
		swapcontext(&curr_run_tcb -> context, &sched_context);
	}
	// directly reaches here when a thread gets a lock
	mutex -> holdingTCB = curr_run_tcb;
	return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
	atomic_flag_clear(&mutex->status); // ATOMIC_FLAG_CLEAR or unlocked
	mutex -> holdingTCB = NULL;
	while(!isRunqueueEmpty(mutex->wq))
	{
		tcb *ntcb = dequeueNext(mutex->wq);
		ntcb -> status = THREAD_READY;
		enqueue(sched_rq[ntcb->priority], ntcb);
	}
	return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{	
	mutex -> holdingTCB = NULL;
	atomic_flag_clear(&mutex->status);
	mutex -> wq = NULL;
	free(mutex->wq);
	return 0;
};




/* scheduler */
static void schedule()
{
	if(SCHED == _PSJF)
		sched_psjf();
	else if(SCHED == _MLFQ)
		sched_mlfq();
	else
		printf("no such scheduling algorithm");
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf()
{
	if(curr_run_tcb -> status == THREAD_RUNNING || curr_run_tcb -> status == THREAD_READY)
	{
		curr_run_tcb -> status = THREAD_READY;
		enqueue(sched_rq[0], curr_run_tcb);
	}
	sort(sched_rq[0]);
	curr_run_tcb = NULL;
	if(runqueueSize(sched_rq[0]) != 0)
		curr_run_tcb = dequeueNext(sched_rq[0]);

	if(curr_run_tcb != NULL)
	{
		if(curr_run_tcb -> status == THREAD_CREATED)
		{
			clock_gettime(CLOCK_REALTIME, &(curr_run_tcb->end_response_time));
			temp_avg_resp_time += (double) (curr_run_tcb->end_response_time.tv_sec - curr_run_tcb->start_time.tv_sec) * 1000 + (curr_run_tcb->end_response_time.tv_nsec - curr_run_tcb->start_time.tv_nsec) / 1000000;
			avg_resp_time = temp_avg_resp_time/(thread_count-1);
		}
		curr_run_tcb -> status = THREAD_RUNNING;
		tot_cntx_switches++;
		curr_run_tcb -> times_scheduled += 1;
		setcontext(&curr_run_tcb->context);
	}
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq()
{
	int curr_priority = curr_run_tcb -> priority;
	if(isYield == 1)
	{
		curr_run_tcb -> status = THREAD_READY;
		enqueue(sched_rq[curr_priority], curr_run_tcb);
		isYield = 0;
	}
	else if(curr_run_tcb->status == THREAD_READY || curr_run_tcb->status == THREAD_RUNNING)
	{
		curr_run_tcb -> status = THREAD_READY;
		if(curr_priority != QUEUE_NUM - 1)
		{
			enqueue(sched_rq[curr_priority++], curr_run_tcb);
			curr_run_tcb->priority = curr_priority;
		}
		else
			enqueue(sched_rq[curr_priority], curr_run_tcb);
	}
	for(int i=0; i<QUEUE_NUM; i++)
	{
		if(runqueueSize(sched_rq[i]) != 0)
		{
			sched_rr(i);
		}
	}
}

/* Round Robin scheduling algorithm */
static void sched_rr(int queue_priority)
{
	curr_run_tcb = dequeueNext(sched_rq[queue_priority]);
	if(curr_run_tcb != NULL)
	{
		if(curr_run_tcb -> status == THREAD_CREATED)
		{
			clock_gettime(CLOCK_REALTIME, &(curr_run_tcb->end_response_time));
			temp_avg_resp_time += (double) (curr_run_tcb->end_response_time.tv_sec - curr_run_tcb->start_time.tv_sec) * 1000 + (curr_run_tcb->end_response_time.tv_nsec - curr_run_tcb->start_time.tv_nsec) / 1000000;
			avg_resp_time = temp_avg_resp_time/(thread_count-1);
		}
		curr_run_tcb -> status = THREAD_RUNNING;
		tot_cntx_switches++;
		setcontext(&curr_run_tcb->context);
	}
}

/* Function to print global statistics */
void print_app_stats(void)
{
	fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
	fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
	fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}



// Queue data structure
Runqueue* init_runqueue()
{
    Runqueue* rq = (Runqueue*)malloc(sizeof(Runqueue));
    rq->head = rq->tail = NULL;
    return rq;
}

void enqueue(Runqueue* rq, tcb* thread)
{
    ThreadNode* newNode = (ThreadNode*)malloc(sizeof(ThreadNode));
    newNode->thread = thread;
    newNode->next = NULL;

    if(rq->tail == NULL)
	{
        rq->head = rq->tail = newNode;
    }
	else
	{
        rq->tail->next = newNode;
        rq->tail = newNode;
    }
}

void dequeue(Runqueue* rq, tcb* threadToDequeue)
{
    if(rq->head == NULL)
	{
        printf("Runqueue is empty dequeue\n");
    }
	else
	{
        ThreadNode* current = rq->head;
        ThreadNode* prev = NULL;

        while (current != NULL)
		{
            if (current->thread == threadToDequeue)
			{
                if(prev == NULL)
				{
                    if (rq->head == rq->tail)
					{
                        rq->head = rq->tail = NULL;
                    }
					else
					{
                        rq->head = rq->head->next;
                    }
                }
				else
				{
                    prev->next = current->next;
                    if(current == rq->tail)
					{
                        rq->tail = prev;
                    }
                }

                free(current);
                return;
            }
            prev = current;
            current = current->next;
        }
        printf("Thread not found in the runqueue\n");
    }
}

int isRunqueueEmpty(Runqueue* rq)
{
    return rq->head == NULL;
}

int runqueueSize(Runqueue* rq)
{
    int size = 0;
    ThreadNode* current = rq->head;
    while (current != NULL)
	{
        size++;
        current = current->next;
    }
    return size;
}

tcb* findTCBInRunqueue(Runqueue* rq, worker_t thread)
{
    ThreadNode* current = rq->head;
    while(current != NULL)
	{
        if (current->thread->id == thread)
		{
            return current->thread;
        }
        current = current->next;
    }
    return NULL;
}

tcb* dequeueNext(Runqueue* rq)
{
    if (rq->head == NULL)
	{
        printf("Runqueue is empty dequeueNext\n");
        return NULL;
    }
	else
	{
        ThreadNode* temp = rq->head;
        tcb* thread = temp->thread;

        if (rq->head == rq->tail)
		{
            rq->head = rq->tail = NULL;
        }
		else
		{
            rq->head = rq->head->next;
        }

        free(temp);
        return thread;
    }
}

void sort(Runqueue* rq)
{
    ThreadNode* current = rq->head;
    while (current != NULL)
	{
        ThreadNode* minNode = current;
        ThreadNode* temp = current->next;

        while (temp != NULL)
		{
            if (temp->thread->times_scheduled < minNode->thread->times_scheduled)
			{
                minNode = temp;
            }

            temp = temp->next;
        }

        if (minNode != current)
		{
            tcb* tempThread = current->thread;
            current->thread = minNode->thread;
            minNode->thread = tempThread;
        }

        current = current->next;
    }
}

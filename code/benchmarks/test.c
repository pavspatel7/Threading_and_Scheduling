#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "../thread-worker.h"
#include "../thread-worker.c"

/* A scratch program template on which to call and
 * test thread-worker library functions as you implement
 * them.
 *
 * You can modify and use this program as much as possible.
 * This will not be graded.
 */

pthread_t t1, t2;
pthread_mutex_t mutex;
int x = 0;
int loop = 10000;

void *add_counter(void *arg) {

    int i;

    /* Add thread synchronizaiton logic in this function */	

    pthread_mutex_lock(&mutex);
    for(i = 0; i < loop; i++)
    {
        x = x + 1;
    }
    pthread_mutex_unlock(&mutex);
	pthread_exit(NULL);
}


// Function to be executed by the first thread
void threadFunction1(void* arg) {
    for (int i = 0; i < 5; ++i) {
        printf("Thread 1: %d\n", i);
    }
    pthread_exit(NULL);
}

// Function to be executed by the second thread
void threadFunction2(void* arg) {
    for (int i = 0; i < 5; ++i) {
        printf("Thread 2: %d\n", i);
    }
    pthread_exit(NULL);
}

int main(int argc, char **argv) {

	/* Implement HERE */

	loop = atoi(argv[1]);

    printf("Going to run four threads to increment x up to %d\n", 2 * loop);

	pthread_mutex_init(&mutex, NULL);

	struct timespec start, end;
        clock_gettime(CLOCK_REALTIME, &start);
    // Create the first thread
    if (pthread_create(&t1, NULL, &add_counter, NULL)!= 0) {
        perror("Error creating thread 1");
        return 1;
    }

    // Create the second thread
    if (pthread_create(&t2, NULL, &add_counter, NULL) != 0) {
        perror("Error creating thread 2");
        return 1;
    }

    // Wait for both threads to finish
   if (pthread_join(t1, NULL) != 0) {
        perror("Error joining thread 1");
        return 1;
    }

    if (pthread_join(t2, NULL) != 0) {
        perror("Error joining thread 2");
        return 1;
    }

    printf("Both threads have finished.\n");

	printf("The final value of x is %d\n", x);

	clock_gettime(CLOCK_REALTIME, &end);

        printf("Total run time: %lu micro-seconds\n", 
	       (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000);

#ifdef USE_WORKERS
        print_app_stats();
        fprintf(stderr, "***************************\n");
#endif

	return 0;
}
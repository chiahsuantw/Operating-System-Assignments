#define _GNU_SOURCE

#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_THREADS 32
#define SCHED_NORMAL 0
#define SCHED_FIFO 1

int NUM_THREADS = 0;
double TIME_WAIT = 0;
pthread_barrier_t barrier;

typedef struct {
  int id;
} thread_info_t;

void *thread_func(void *arg) {
  // Wait until all threads are ready
  pthread_barrier_wait(&barrier);

  // Do some heavy tasks
  thread_info_t *thread_info = (thread_info_t *)arg;
  for (int i = 0; i < 3; i++) {
    printf("Thread %d is running\n", thread_info->id);
    // Busy for <TIME_WAIT> seconds
    time_t start, current = 0;
    time(&start);
    do {
      time(&current);
    } while (difftime(current, start) < TIME_WAIT);
  }

  // Exit the function
  pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
  int policies[MAX_THREADS] = {0};
  int priorities[MAX_THREADS] = {0};

  // Parse program arguments
  int option, index;
  char *token;
  while ((option = getopt(argc, argv, "n:t:s:p:")) != -1) {
    switch (option) {
      case 'n':
        NUM_THREADS = strtol(optarg, NULL, 10);
        if (NUM_THREADS > MAX_THREADS) {
          printf("%s: maximum number of threads allowed = 32\n", argv[0]);
          exit(1);
        }
        break;
      case 't':
        TIME_WAIT = strtod(optarg, NULL);
        break;
      case 's':
        index = 0;
        token = strtok(optarg, ",");
        while (token != NULL && index < MAX_THREADS) {
          policies[index] = !strcmp(token, "FIFO") ? SCHED_FIFO : SCHED_NORMAL;
          token = strtok(NULL, ",");
          index++;
        }
        break;
      case 'p':
        index = 0;
        token = strtok(optarg, ",");
        while (token != NULL && index <= MAX_THREADS) {
          priorities[index] = strtol(token, NULL, 10);
          token = strtok(NULL, ",");
          index++;
        }
        break;
    }
  }

  // Set CPU affinity for the main thread
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  CPU_SET(0, &cpu_set);
  sched_setaffinity(getpid(), sizeof(cpu_set), &cpu_set);

  // Declare worker threads and related resources
  pthread_t threads[NUM_THREADS];
  pthread_attr_t thread_attr[NUM_THREADS];
  thread_info_t thread_info[NUM_THREADS];
  struct sched_param thread_sched_param[NUM_THREADS];
  pthread_barrier_init(&barrier, NULL, NUM_THREADS);

  // Set attributes and run threads
  for (int i = 0; i < NUM_THREADS; i++) {
    thread_info[i].id = i;
    pthread_attr_init(&thread_attr[i]);
    pthread_attr_setaffinity_np(&thread_attr[i], sizeof(cpu_set), &cpu_set);
    pthread_attr_setschedpolicy(&thread_attr[i], policies[i]);
    pthread_attr_setinheritsched(&thread_attr[i], PTHREAD_EXPLICIT_SCHED);
    thread_sched_param[i].sched_priority = priorities[i];
    pthread_attr_setschedparam(&thread_attr[i], &thread_sched_param[i]);
    pthread_create(&threads[i], &thread_attr[i], thread_func, &thread_info[i]);
  }

  // Wait for all threads to finish
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }

  // Destroy resources
  for (int i = 0; i < NUM_THREADS; i++) pthread_attr_destroy(&thread_attr[i]);
  pthread_barrier_destroy(&barrier);
  return 0;
}

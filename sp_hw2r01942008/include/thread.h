#ifndef _THREAD_H_
#define _THREAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

typedef struct thread thread;
typedef struct task_thread task_thread;
typedef struct task_thread_arg task_thread_arg;
typedef struct thread_pool thread_pool;

/* basic thread */
struct thread {
  pthread_t tid;
  void* (*thread_func)(void* arg);
  void* arg;
};

void init_thread(thread** t, void* (*func)(void* arg), void* arg);
void destroy_thread(thread** t);

/* argument pass to task_thread */
struct task_thread_arg {
  void* input;
  void* output;
  void (*func)(void* input, void* output);
  //void (*callback)(void* arg);
  //void* callback_arg;
};

struct task_thread {
  /* concrete thread instance */
  thread* thread_instance;
  /* lock used for waiting new task */
  pthread_mutex_t control_lock;
  /* argument for a new task */
  task_thread_arg arg;
  /* thread state for thread pool picking a free thread */
  int isBusy;
};

void init_task_thread(task_thread** t);
void destroy_task_thread(task_thread** t);
/* launch thread by release controll lock */
void launch(task_thread* t, task_thread_arg* arg);
/* real thread run function of task_thread */
void* task_thread_func(void* arg);

struct thread_pool {
  int thread_num;
  task_thread** threads;
  /* lock used when picking a free thread */
  pthread_mutex_t dispatch_lock;
};

void init_thread_pool(thread_pool** pool, int num);
/* dispatch a task to a thread */
int run_task(thread_pool* pool, task_thread_arg* arg);
/* check number of working thread */
int check_working(thread_pool *pool);
void destroy_thread_pool(thread_pool** pool);

#ifdef __cplusplus
}
#endif

#endif

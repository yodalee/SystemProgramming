#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "thread.h"

void
init_task_thread(task_thread** t)
{
  task_thread *tmp = (task_thread*)malloc(sizeof(task_thread));
  memset(tmp, 0, sizeof(task_thread));
  tmp->thread_instance = (thread*)malloc(sizeof(thread));
  pthread_mutex_init(&(tmp->control_lock), NULL);
  *t = tmp;
}

void 
destroy_task_thread(task_thread **t) 
{
  task_thread *tmp = *t;
  free(tmp->thread_instance);
  free(tmp);
}

void 
launch(task_thread *t, task_thread_arg *arg) 
{
}

void 
init_thread_pool(thread_pool **pool, int num) 
{
  thread_pool *tmp = (thread_pool*)malloc(sizeof(thread_pool));
  if (!tmp) {
    fprintf(stderr, "thread pool malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(thread_pool));
  tmp->thread_num = num;
  tmp->threads = (task_thread**)malloc(num*sizeof(task_thread));
  //initial task thread
  int i = 0;
  for (i = 0; i < num; ++i) {
    init_task_thread(&tmp->threads[i]);
  }
  pthread_mutex_init(&(tmp->dispatch_lock), NULL);
  *pool = tmp;
}

int 
run_task(thread_pool *pool, task_thread_arg* arg) 
{
  //find a free task_thread
  int found = 0;
  int i;
  for (i = 0; i < pool->thread_num; ++i) {
    if (!(pool->threads[i]->isBusy)) {
      found = i+1;
      launch(pool->threads[i], arg);
      break;
    } 
  }
  return found;
}

void 
destroy_thread_pool(thread_pool **pool) 
{
  thread_pool *tmp = *pool;
  int i;
  for (i = 0; i < tmp->thread_num; ++i) {
    destroy_task_thread(&tmp->threads[i]);
  }
  free(tmp);
}

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
  tmp->thread_instance->arg = NULL;
  pthread_mutex_init(&(tmp->control_lock), NULL);
  pthread_mutex_lock(&(tmp->control_lock));
  if(pthread_create(&(tmp->thread_instance->tid), NULL,
        &task_thread_func, tmp)){
    fprintf(stderr, "cannot create thread\n");
    return;
  }
  pthread_mutex_unlock(&tmp->control_lock);
  *t = tmp;
}

void 
destroy_task_thread(task_thread **t) 
{
  //call thread stop
  task_thread *tmp = *t;
  pthread_cancel(tmp->thread_instance->tid);
  free(tmp->thread_instance);
  free(tmp);
}

void 
launch(task_thread *t, task_thread_arg *arg) 
{
  if (pthread_mutex_lock(&(t->control_lock))) {
    perror("pthread_mutex_lock: in thread");
  }
  t->isBusy = 1;
  t->arg = *arg;
  t->thread_instance->arg = (void*)(&(t->arg));
  if (pthread_mutex_unlock(&(t->control_lock))) {
    perror("pthread_mutex_unlock: in thread");
  }
}

void* 
task_thread_func(void *arg)
{
  task_thread *task = (task_thread*)arg;
  while (1) {
    pthread_mutex_lock(&(task->control_lock));
    if (task->thread_instance->arg == NULL) {
      //currently no work to do
      pthread_mutex_unlock(&(task->control_lock));
    } else {
      //process it by calling the func
      task->arg.func(task->arg.input, task->arg.output);
      task->isBusy = 0;
      task->thread_instance->arg = NULL;
      pthread_mutex_unlock(&(task->control_lock));
    }
  }
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
  tmp->threads = (task_thread**)malloc(num*sizeof(task_thread*));
  //initial task thread
  int i = 0;
  for (i = 0; i < num; ++i) {
    init_task_thread(&(tmp->threads[i]));
  }
  pthread_mutex_init(&(tmp->dispatch_lock), NULL);
  *pool = tmp;
}

int 
run_task(thread_pool *pool, task_thread_arg* arg) 
{
  //find a free task_thread
  int found = -1;
  int i;
  pthread_mutex_lock(&(pool->dispatch_lock));
  for (i = 0; i < pool->thread_num; ++i) {
    if (!(pool->threads[i]->isBusy)) {
      found = i;
      launch(pool->threads[i], arg);
      break;
    } 
  }
  pthread_mutex_unlock(&(pool->dispatch_lock));
  return found;
}

void 
destroy_thread_pool(thread_pool **pool) 
{
  thread_pool *tmp = *pool;
  int i;
  for (i = 0; i < tmp->thread_num; ++i) {
    destroy_task_thread(&(tmp->threads[i]));
  }
  free(tmp);
}

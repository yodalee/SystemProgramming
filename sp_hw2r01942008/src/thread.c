#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "thread.h"

void
init_task_thread(task_thread** t)
{
  task_thread *tmp = (task_thread*)malloc(sizeof(task_thread));
  tmp->thread_instance = (thread*)malloc(sizeof(thread));
  tmp->thread_instance->arg = NULL;
  pthread_mutex_init(&(tmp->control_lock), NULL);
  pthread_mutex_lock(&(tmp->control_lock));
  tmp->isBusy = 0;
  if(pthread_create(&(tmp->thread_instance->tid), NULL,
        &task_thread_func, tmp)){
    fprintf(stderr, "cannot create thread\n");
    return;
  }
  *t = tmp;
}

void 
destroy_task_thread(task_thread **t) 
{
  //call thread stop
  task_thread *tmp = *t;
  pthread_cancel(tmp->thread_instance->tid);
  free(tmp->thread_instance);
  pthread_mutex_destroy(&(tmp->control_lock));
  free(tmp);
}

void 
launch(task_thread *t, task_thread_arg *arg) 
{
  t->isBusy = 1;
  t->arg = *arg;
  t->thread_instance->arg = (void*)(&(t->arg));
  printf("%x, %x\n", t->thread_instance->arg, &(t->arg));
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
    printf("%x\n", &(task->arg));
    //process it by calling the func
    task->arg.func(task->arg.input, task->arg.output);
    task->isBusy = 0;
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
  pthread_mutex_destroy(&(tmp->dispatch_lock));
  free(tmp);
}

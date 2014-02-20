/* Simplethreads Instructional Thread Package
 * 
 * sthread_user.c - Implements the sthread API using user-level threads.
 *
 *    You need to implement the routines in this file.
 *
 * Change Log:
 * 2002-04-15        rick
 *   - Initial version.
 * 2005-10-12        jccc
 *   - Added semaphores, deleted conditional variables
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <limits.h>

#include "redblacktree.h"
#include <sthread.h>
#include <sthread_user.h>
#include <sthread_time_slice.h>
#include <sthread_user.h>
#include "aux.h"





static queue_t *dead_thr_list;		/* lista de threads "mortas" */
static queue_t *sleep_thr_list;		/* lista de threads a "dormir" */
static queue_t *join_thr_list;		/* lista de threads no join*/
static queue_t *zombie_thr_list;	/* lista de threads "zombie"*/
static struct _sthread *active_thr;	/* thread activa */
static int tid_gen;					/* gerador de tid's */

#define MIN_DELAY 5
#define MAX_INC 100

#define CLOCK_TICK 10000
static long Clock;


/*********************************************************************/
/* Part 1: Creating and Scheduling Threads                           */
/*********************************************************************/


void sthread_user_free(struct _sthread *thread);

void sthread_aux_start(void){
  splx(LOW);
  active_thr->start_routine_ptr(active_thr->args);
  sthread_user_exit((void*)0);
}

// sthread_nice - sets the nice value and returns the new 'priority'(priority + nice)
int sthread_nice(int nice){
	if(nice > 10){	
		printf("nice value to high, value used: 10");
		nice=10;
	}
	if(nice < 0){	
		printf("nice value to low, value used: 0");
		nice=0;
	}
 	active_thr->nice=nice;
 	return active_thr->priority + nice;
}

void sthread_user_dispatcher(void);

void sthread_user_init(void) {

  exe_thr_tree = rbCreate();
  dead_thr_list = create_queue();
  sleep_thr_list = create_queue();
  join_thr_list = create_queue();
  zombie_thr_list = create_queue();
  blocked_thr_list = create_blocked();
  
  tid_gen = 1;

  struct _sthread *main_thread = malloc(sizeof(struct _sthread));
  main_thread->start_routine_ptr = NULL;
  main_thread->args = NULL;
  main_thread->saved_ctx = sthread_new_blank_ctx();
  main_thread->wake_time = 0;
  main_thread->join_tid = 0;
  main_thread->join_ret = NULL;
  main_thread->tid = tid_gen++;
  main_thread->priority = 1;
  main_thread->nice = 0;
  main_thread->vruntime = 0;
  main_thread->runtime = 0;
  main_thread->waittime = 0;
  main_thread->sleeptime = 0;
 
  
  active_thr = main_thread;

  Clock = 1;
  sthread_time_slices_init(sthread_user_dispatcher, CLOCK_TICK);
}


sthread_t sthread_user_create(sthread_start_func_t start_routine, void *arg, int priority)
{
  struct _sthread *new_thread = (struct _sthread*)malloc(sizeof(struct _sthread));
  if(priority > 10){	
		printf("priority value to high, value used: 10");
		priority=10;
	}
	if(priority < 1){	
		printf("priority value to low, value used: 1");
		priority=0;
	}
  sthread_ctx_start_func_t func = sthread_aux_start;
  new_thread->args = arg;
  new_thread->start_routine_ptr = start_routine;
  new_thread->wake_time = 0;
  new_thread->join_tid = 0;
  new_thread->join_ret = NULL;
  new_thread->saved_ctx = sthread_new_ctx(func);
  new_thread->priority = priority;
  new_thread->nice = 0;
  new_thread->runtime = 0;
  new_thread->waittime = 0;
  new_thread->sleeptime = 0;
  if(emptyTree(exe_thr_tree))
	new_thread->vruntime = 0;
  else
  	new_thread->vruntime = exe_thr_tree->prioritary->thread->vruntime;
  
  splx(HIGH);
  new_thread->tid = tid_gen++;
  rbTreeInsert(exe_thr_tree, new_thread);
  splx(LOW);
  return new_thread;
}


void sthread_user_exit(void *ret) {
  splx(HIGH);
   
   int is_zombie = 1;

   // unblock threads waiting in the join list
   queue_t *tmp_queue = create_queue();   
   while (!queue_is_empty(join_thr_list)) {
      struct _sthread *thread = queue_remove(join_thr_list);
     
      //printf("Test join list: join_tid=%d, active->tid=%d\n", thread->join_tid, active_thr->tid);

      if (thread->join_tid == active_thr->tid) {
         thread->join_ret = ret;
         rbTreeInsert(exe_thr_tree,thread);
         is_zombie = 0;
      } else {
         queue_insert(tmp_queue,thread);
      }
   }
   delete_queue(join_thr_list);
   join_thr_list = tmp_queue;
 
   if (is_zombie) {
      queue_insert(zombie_thr_list, active_thr);
   } else {
      queue_insert(dead_thr_list, active_thr);
   }
   

   if(emptyTree(exe_thr_tree)){  /* pode acontecer se a unica thread em execucao fizer */
    rbTreeDestroy(exe_thr_tree);              /* sthread_exit(0). Este codigo garante que o programa sai bem. */
    delete_queue(dead_thr_list);
    destroyBlocked(blocked_thr_list);
    sthread_user_free(active_thr);
    printf("Exec queue is empty!\n");
    exit(0);
  }

  
   // remove from exec list
   struct _sthread *old_thr = active_thr;
   active_thr = treeRemove(exe_thr_tree);
   sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

   splx(LOW);
}

//Updates Vruntime by adding priority*nice
void UpdtVruntime(){
	active_thr->vruntime = active_thr->vruntime + 1*((active_thr->priority)+(active_thr->nice));
}

//Updates Runtime by adding 1
void UpdtRuntime(){
	active_thr->runtime = active_thr->runtime + 1;
}

//Updates Waittime
void UpdtWaitTime(rbnode node,int delay){
	node->thread->waittime += delay;
}

void UpdtSleeptime(int delay){
	 incBlocked(blocked_thr_list, delay);
	 incQueue(sleep_thr_list, delay);
	 incQueue(join_thr_list, delay);
}

//Treats Overflow by subtractin vruntime value(of the overflowing thread) on all threads
void treatOverflow(rbtree tree,blocked* blocked, queue_t *sleep,  queue_t *join){
	long dec = active_thr->vruntime;
	decTree(tree, dec);
	decBlocked(blocked, dec);
	decQueue(sleep, dec);
	decQueue(join, dec);	 
	active_thr->vruntime = 0;
}

//increments all the timers of a threads and wakes the sleeping therads when its time
void sthread_user_dispatcher(void)
{
   int static delay = 0;
   Clock++;
   queue_t *tmp_queue = create_queue();   

   while (!queue_is_empty(sleep_thr_list)) {
      struct _sthread *thread = queue_remove(sleep_thr_list);
      
      if (thread->wake_time == Clock) {
         thread->wake_time = 0;
         thread->sleeptime +=1;
         rbTreeInsert(exe_thr_tree,thread);
      } else {
         queue_insert(tmp_queue,thread);
      }
   }
   delete_queue(sleep_thr_list);
   sleep_thr_list = tmp_queue;
   if(active_thr->vruntime >= INT_MAX  - MAX_INC)
  	treatOverflow(exe_thr_tree, blocked_thr_list, sleep_thr_list, join_thr_list);
   UpdtVruntime();
   UpdtRuntime();
   UpdtSleeptime(1);
   travers(exe_thr_tree,1);
   if(delay < MIN_DELAY)
		delay++;
	else{
		if(exe_thr_tree->prioritary->thread != NULL){
			if(active_thr->vruntime < exe_thr_tree->prioritary->thread->vruntime){
				delay++;
				return;
			}
			delay=0;
		   	sthread_user_yield();
		}
	}
}


void sthread_user_yield(void)
{
	splx(HIGH);
	if(!emptyTree(exe_thr_tree)){
		struct _sthread *old_thr;
		old_thr = active_thr;
		active_thr = treeRemove(exe_thr_tree);
		rbTreeInsert(exe_thr_tree, old_thr);
	  	sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
	}
	splx(LOW);
}




void sthread_user_free(struct _sthread *thread)
{
  sthread_free_ctx(thread->saved_ctx);
  free(thread);
}

// prints the information of a Queue
void dumpQueue(queue_t *queue){
	queue_element_t *ptr;
	
	for(ptr= queue->first; ptr != queue->last; ptr=ptr->next){
		printf("id: %d priority: %d vruntime: %ld\nruntime: %ld sleeptime: %ld waittime: %ld\n\n", 	ptr->thread->tid,
																								 	ptr->thread->priority, 
																								 	ptr->thread->vruntime, 
																								 	ptr->thread->runtime, 
																								 	ptr->thread->sleeptime, 
																								 	ptr->thread->waittime);
	}
	if(ptr != NULL)
		printf("id: %d priority: %d vruntime: %ld\nruntime: %ld sleeptime: %ld waittime: %ld\n\n", 	ptr->thread->tid, 
																									ptr->thread->priority, 
																									ptr->thread->vruntime, 
																									ptr->thread->runtime, 
																									ptr->thread->sleeptime, 
																									ptr->thread->waittime);
}

//prints the information of Blocked(structure that contains queues of blocked threads 
void dumpBlocked(blocked* blocked){
	int i;
	block * ptr;
	for(i=1, ptr= blocked->mutexs;ptr!=NULL; i++, ptr=ptr->next){
		printf("----Mutex %d----\n\n", i);
		if(!queue_is_empty(ptr->queue)){
			QuickSortBlocked(ptr->queue->first, ptr->queue->last);
			dumpQueue(ptr->queue);
		}
	}
	
	for(i=1, ptr= blocked->monitors;ptr!=NULL; i++, ptr=ptr->next){
		printf("----Monitor %d----\n\n", i);
		if(!queue_is_empty(ptr->queue)){
			QuickSortBlocked(ptr->queue->first, ptr->queue->last);
			dumpQueue(ptr->queue);
		}
	}
}
	
// Dump the information of all threads
void sthread_dump(){
	
	printf("=== dump start ===\n Clock = %ld\n\n active thread\n", Clock);
	printf("id: %d priority: %d vruntime: %ld\nruntime: %ld sleeptime: %ld waittime: %ld\n\n", active_thr->tid, active_thr->priority, active_thr->vruntime, active_thr->runtime, active_thr->sleeptime, active_thr->waittime );
	
	dumpTree(exe_thr_tree);
	printf(">>>>SleepList<<<<\n\n");
	if(!queue_is_empty(sleep_thr_list)){
		QuickSortSleep(sleep_thr_list->first,sleep_thr_list->last);
		dumpQueue(sleep_thr_list);
	}
	printf(">>>>BlockedList<<<<\n\n");
	dumpBlocked(blocked_thr_list);
	printf("==== Dump End ====\n");
}


/*********************************************************************/
/* Part 2: Join and Sleep Primitives                                 */
/*********************************************************************/

int sthread_user_join(sthread_t thread, void **value_ptr)
{
   /* suspends execution of the calling thread until the target thread
      terminates, unless the target thread has already terminated.
      On return from a successful pthread_join() call with a non-NULL 
      value_ptr argument, the value passed to pthread_exit() by the 
      terminating thread is made available in the location referenced 
      by value_ptr. When a pthread_join() returns successfully, the 
      target thread has been terminated. The results of multiple 
      simultaneous calls to pthread_join() specifying the same target 
      thread are undefined. If the thread calling pthread_join() is 
      canceled, then the target thread will not be detached. 

      If successful, the pthread_join() function returns zero. 
      Otherwise, an error number is returned to indicate the error. */

   
   splx(HIGH);
   // checks if the thread to wait is zombie
   int found = 0;
   queue_t *tmp_queue = create_queue();
   while (!queue_is_empty(zombie_thr_list)) {
      struct _sthread *zthread = queue_remove(zombie_thr_list);
      if (thread->tid == zthread->tid) {
         *value_ptr = thread->join_ret;
         queue_insert(dead_thr_list,thread);
         found = 1;
      } else {
         queue_insert(tmp_queue,zthread);
      }
   }
   delete_queue(zombie_thr_list);
   zombie_thr_list = tmp_queue;
  
   if (found) {
       splx(LOW);
       return 0;
   }

   
   // search active queue
   if (active_thr->tid == thread->tid) {
      found = 1;
   }
   
   queue_element_t *qe = NULL;
   rbnode qee = exe_thr_tree->nil;

   // search exe
   
   qee = rbSearch(exe_thr_tree , exe_thr_tree->first->left, thread->tid, thread->vruntime);
   if (qee != exe_thr_tree->nil){
      found = 1;
   }
   

   // search sleep
   qe = sleep_thr_list->first;
   while (!found && qe != NULL) {
      if (qe->thread->tid == thread->tid) {
         found = 1;
      }
      qe = qe->next;
   }

   // search join
   qe = join_thr_list->first;
   while (!found && qe != NULL) {
      if (qe->thread->tid == thread->tid) {
         found = 1;
      }
      qe = qe->next;
   }

   // if found blocks until thread ends
   if (!found) {
      splx(LOW);
      return -1;
   } else {
      active_thr->join_tid = thread->tid;
      
      struct _sthread *old_thr = active_thr;
      queue_insert(join_thr_list, old_thr);
      active_thr = treeRemove(exe_thr_tree);
      sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
  
      *value_ptr = thread->join_ret;
   }
   
   splx(LOW);
   return 0;
}


/* minimum sleep time of 1 clocktick.
  1 clock tick, value 10 000 = 10 ms */

int sthread_user_sleep(int time)
{
   splx(HIGH);
   
   long num_ticks = time / CLOCK_TICK;
   if (num_ticks == 0) {
      splx(LOW);
      
      return 0;
   }
   active_thr->wake_time = Clock + num_ticks;
      
   queue_insert(sleep_thr_list,active_thr);
   sthread_t old_thr = active_thr;
   active_thr = treeRemove(exe_thr_tree);
   if(active_thr!=NULL)
   	sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
   splx(LOW);
   return 0;
}

/* --------------------------------------------------------------------------*
 * Synchronization Primitives                                                *
 * ------------------------------------------------------------------------- */

/*
 * Mutex implementation
 */

struct _sthread_mutex
{
  lock_t l;
  struct _sthread *thr;
  queue_t* queue;
};

sthread_mutex_t sthread_user_mutex_init()
{
  sthread_mutex_t lock;

  if(!(lock = malloc(sizeof(struct _sthread_mutex)))){
    printf("Error in creating mutex\n");
    return 0;
  }

  /* mutex initialization */
  lock->l=0;
  lock->thr = NULL;
  lock->queue = create_queue();
  addMutex(blocked_thr_list, lock->queue);
  
  return lock;
}

void sthread_user_mutex_free(sthread_mutex_t lock)
{
  removeMutex(blocked_thr_list, lock->queue);
  delete_queue(lock->queue);
  free(lock);
}

void sthread_user_mutex_lock(sthread_mutex_t lock)
{
  while(atomic_test_and_set(&(lock->l))) {}

  if(lock->thr == NULL){
    lock->thr = active_thr;

    atomic_clear(&(lock->l));
  } else {
  	active_thr->blockstart=Clock;
    queue_insert(lock->queue, active_thr);
    
    atomic_clear(&(lock->l));

    splx(HIGH);
    struct _sthread *old_thr;
    old_thr = active_thr;
    //rbTreeInsert(exe_thr_tree, old_thr);
    active_thr = treeRemove(exe_thr_tree);
    sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

    splx(LOW);
  }
}

void sthread_user_mutex_unlock(sthread_mutex_t lock)
{
  if(lock->thr!=active_thr){
    printf("unlock without lock!\n");
    return;
  }

  while(atomic_test_and_set(&(lock->l))) {}

  if(queue_is_empty(lock->queue)){
    lock->thr = NULL;
  } else {
    lock->thr = queue_remove(lock->queue);
    rbTreeInsert(exe_thr_tree, lock->thr);
  }

  atomic_clear(&(lock->l));
}

/*
 * Monitor implementation
 */

struct _sthread_mon {
 	sthread_mutex_t mutex;
	queue_t* queue;
};

sthread_mon_t sthread_user_monitor_init()
{
  sthread_mon_t mon;
  if(!(mon = malloc(sizeof(struct _sthread_mon)))){
    printf("Error creating monitor\n");
    return 0;
  }

  mon->mutex = sthread_user_mutex_init();
  mon->queue = create_queue();
  addMonitor(blocked_thr_list, mon->queue);
  return mon;
}

void sthread_user_monitor_free(sthread_mon_t mon)
{
  sthread_user_mutex_free(mon->mutex);
  removeMonitor(blocked_thr_list, mon->queue);
  delete_queue(mon->queue);
  free(mon);
}

void sthread_user_monitor_enter(sthread_mon_t mon)
{
  sthread_user_mutex_lock(mon->mutex);
}

void sthread_user_monitor_exit(sthread_mon_t mon)
{
  sthread_user_mutex_unlock(mon->mutex);
}

void sthread_user_monitor_wait(sthread_mon_t mon)
{
  struct _sthread *temp;

  if(mon->mutex->thr != active_thr){
    printf("monitor wait called outside monitor\n");
    return;
  }

  /* inserts thread in queue of blocked threads */
  temp = active_thr;
  temp->blockstart=Clock;
  queue_insert(mon->queue, temp);

  /* exits mutual exclusion region */
  sthread_user_mutex_unlock(mon->mutex);

  splx(HIGH);
  struct _sthread *old_thr;
  old_thr = active_thr;
  active_thr = treeRemove(exe_thr_tree);
  sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
  splx(LOW);
}

void sthread_user_monitor_signal(sthread_mon_t mon)
{
  struct _sthread *temp;

  if(mon->mutex->thr != active_thr){
    printf("monitor signal called outside monitor\n");
    return;
  }

  while(atomic_test_and_set(&(mon->mutex->l))) {}
  if(!queue_is_empty(mon->queue)){
    /* changes blocking queue for thread */
    temp = queue_remove(mon->queue);
    queue_insert(mon->mutex->queue, temp);
  }
  atomic_clear(&(mon->mutex->l));
}

void sthread_user_monitor_signalall(sthread_mon_t mon)
{
  struct _sthread *temp;

  if(mon->mutex->thr != active_thr){
    printf("monitor signalall called outside monitor\n");
    return;
  }

  while(atomic_test_and_set(&(mon->mutex->l))) {}
  while(!queue_is_empty(mon->queue)){
    /* changes blocking queue for thread */
    temp = queue_remove(mon->queue);
    queue_insert(mon->mutex->queue, temp);
  }
  atomic_clear(&(mon->mutex->l));
}


/* The following functions are dummies to 
 * highlight the fact that pthreads do not
 * include monitors.
 */

sthread_mon_t sthread_dummy_monitor_init()
{
   printf("WARNING: pthreads do not include monitors!\n");
   return NULL;
}


void sthread_dummy_monitor_free(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}


void sthread_dummy_monitor_enter(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}


void sthread_dummy_monitor_exit(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}


void sthread_dummy_monitor_wait(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}


void sthread_dummy_monitor_signal(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}

void sthread_dummy_monitor_signalall(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}


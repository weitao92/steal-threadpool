      /**
       * threadpool.c
       *
       * A work-stealing, fork-join thread pool.
       */

      /* 
       * Opaque forward declarations. The actual definitions of these 
       * types will be local to your threadpool.c implementation.
       */
      #include "threadpool.h"
      #include "list.h"
      #include <pthread.h>
      #include <stdlib.h>
      #include <string.h>
      #include <stdio.h>
      #include <stdint.h>
      #include <stdbool.h>
      #include <semaphore.h>

#define DEBUG 0


struct thread_pool
{
	int numGlobTasks;
	struct thread* threadList;
	int poolSize;
	struct list globalQueue;
	pthread_mutex_t lock;
	sem_t moreWork;
	bool shutdown;
	bool flag;
	pthread_cond_t condition;
};

struct thread
{
	pthread_t pid;
	struct list localQueue;
	struct thread_pool* tPool;
	pthread_mutex_t tLock;
};


struct future
{
	fork_join_task_t newTask;
	void* result;
	struct thread_pool* tPool;
	struct list_elem elem;
	void* args;
	int state;
	pthread_mutex_t fLock;
	//0 = just created
    //1 = executing
    //2 = done

	sem_t sem;


};

static __thread struct thread* currentThread = NULL;
      //static __thread int id;

static void * threadFunction( void * temp)
{
	
	currentThread = (struct thread*) temp;
	struct thread_pool* p = currentThread->tPool;

	while(true)
	{
		pthread_mutex_lock(&p->lock);
		while(list_empty(&p->globalQueue) && !p->shutdown)
		{
			pthread_cond_wait(&p->condition, &p->lock);
		}

		if(p->shutdown)
		{
			pthread_mutex_unlock(&p->lock);
			break;
		}



		struct future *f = NULL;

		struct list_elem* element = list_pop_back(&p->globalQueue);
		f = list_entry(element, struct future, elem);
		
		if(f != NULL)
		{

			pthread_mutex_lock(&f->fLock);
			f->state = 1;
			pthread_mutex_unlock(&f->fLock);
			pthread_mutex_unlock(&p->lock);
			
			f->result = f->newTask(p, f->args);
			pthread_mutex_lock(&p->lock);
			pthread_mutex_lock(&f->fLock);
			f->state = 2;
			sem_post(&f->sem);
			pthread_mutex_unlock(&f->fLock);
		}

		p->flag = false;
		pthread_mutex_unlock(&p->lock);

	}

	return NULL;
}


/* Create a new thread pool with no more than n threads. */
struct thread_pool * thread_pool_new(int nthreads)
{
	struct thread_pool* pool = malloc(sizeof(struct thread_pool));
	list_init(&pool->globalQueue);
	pool->threadList = malloc((nthreads) * sizeof(struct thread));
	pool->poolSize = nthreads;
	pool->numGlobTasks = 0;  
	pool->shutdown = false;
	pool->flag = false;
	pthread_cond_init(&pool->condition, NULL);
	sem_init(&pool->moreWork, 0, 0);
	for(int i = 0; i < nthreads; i++)
	{
		struct thread* tempThread = malloc(sizeof(struct thread));
		tempThread->tPool = pool;
		list_init(&tempThread->localQueue);
		pthread_mutex_init(&tempThread->tLock, NULL);

		pool->threadList[i] = *tempThread;  
		pthread_create(&pool->threadList[i].pid, NULL, threadFunction, tempThread);
	}
	return pool;
}

      /* 
       * Shutdown this thread pool in an orderly fashion.  
       * Tasks that have been submitted but not executed may or
       * may not be executed.
       *
       * Deallocate the thread pool object before returning. 
       */
void thread_pool_shutdown_and_destroy(struct thread_pool * th_pool)
{
	pthread_mutex_lock(&th_pool->lock);
	th_pool->shutdown = true;
	pthread_cond_broadcast(&th_pool->condition);
	pthread_mutex_unlock(&th_pool->lock);

	if(th_pool != NULL)
	{
		for(int i = 0; i < th_pool->poolSize; i++)
		{
			if(pthread_join((th_pool->threadList[i]).pid, NULL)!=0)
			{
				printf("ERRROOOOORR!");
			}
		}
		free(th_pool->threadList);
		free(th_pool);
	}
}

      /* 
       * Submit a fork join task to the thread pool and return a
       * future.  The returned future can be used in future_get()
       * to obtain the result.
       * 'pool' - the pool to which to submit
       * 'task' - the task to be submitted.
       * 'data' - data to be passed to the task's function
       *
       * Returns a future representing this computation.
       */
struct future * thread_pool_submit(
	struct thread_pool *pool, 
	fork_join_task_t task, 
	void * data)
{	
	struct future* newFuture = malloc(sizeof(struct future));
	newFuture->tPool = pool;
	newFuture->newTask = task;
	newFuture->args = data;
	newFuture->result = NULL;
	newFuture->state = 0;
	pthread_mutex_init(&newFuture->fLock, NULL);
	sem_init(&newFuture->sem, 0, 0);

	pthread_mutex_lock(&pool->lock);
	list_push_front( &pool->globalQueue,&newFuture->elem);
	
	pool->flag = true;
	pthread_cond_broadcast(&pool->condition);
	pthread_mutex_unlock(&pool->lock);
	
	return newFuture;
}

      /* Make sure that the thread pool has completed the execution
       * of the fork join task this future represents.
       *
       * Returns the value returned by this task.
       */
void * future_get(struct future * f)
{
	pthread_mutex_lock(&f->tPool->lock);
	pthread_mutex_lock(&f->fLock);
	if(f->state == 0)
	{
		list_remove(&f->elem);
		
		f->state = 1;
		pthread_mutex_unlock(&f->fLock);
		pthread_mutex_unlock(&f->tPool->lock);

		f->result = f->newTask(f->tPool, f->args);
		pthread_mutex_lock(&f->tPool->lock);
		pthread_mutex_lock(&f->fLock);
		f->state = 2;
		
		sem_post(&f->sem);
		pthread_mutex_unlock(&f->fLock);
		pthread_mutex_unlock(&f->tPool->lock);

		
	}
	else if (f->state == 1)
	{		
		pthread_mutex_unlock(&f->fLock);
		
		pthread_mutex_unlock(&f->tPool->lock);

		sem_wait(&f->sem);
	}
	else
	{
		pthread_mutex_unlock(&f->fLock);		
		pthread_mutex_unlock(&f->tPool->lock);
	}

	return f->result;
}

/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future * f)
{
	free(f);
}


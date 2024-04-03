#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

// Optional: use these functions to add debug or error prints to your application
//#define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    usleep(1000 * (thread_func_args->wait_to_obtain_ms)); /* sleep for wait_to_obtain_ms */

    /* int pthread_mutex_lock(pthread_mutex_t *mutex); 
    *  int pthread_mutex_unlock(pthread_mutex_t *mutex);
    *
    * If successful, functions shall return zero
    */
    int ret;
    ret = pthread_mutex_lock(thread_func_args->mutex);
    if (ret != 0)
        return thread_param;

    usleep(1000 * (thread_func_args->wait_to_release_ms)); /* sleep for wait_to_release_ms */

    ret = pthread_mutex_unlock(thread_func_args->mutex);
    if (ret != 0)
        return thread_param;

    thread_func_args->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    /* void *malloc(size_t size); 
    *
    * return a pointer to the allocated memory, which is suitably
    *   aligned for any type that fits into the requested size or less.
    *   On error, these functions return NULL
    */
    struct thread_data* thread_func_args = (struct thread_data*)malloc(sizeof(struct thread_data));
    /* check if memory allocation was succesful */
    if (thread_func_args == NULL) 
        return false; /* memory allocation failed */

    
    thread_func_args->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_func_args->wait_to_release_ms = wait_to_release_ms;
    thread_func_args->mutex = mutex;
    thread_func_args->thread_complete_success = false;

    int ret;
    /*       int pthread_create(pthread_t *restrict thread,
    *                      const pthread_attr_t *restrict attr,
    *                      void *(*start_routine)(void *),
    *                      void *restrict arg);
    * 
    * On success, pthread_create() returns 0;
    */
    ret = pthread_create(thread, NULL, threadfunc, thread_func_args);
    if(ret == 0)
        return true;

    /* should we free the memory? */
    free(thread_func_args); 
    return false;
}


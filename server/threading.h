#ifndef _THREADING_H_
#define	_THREADING_H_

#include <stdbool.h>
#include <pthread.h>

/**
 * This structure should be dynamically allocated and passed as
 * an argument to your thread using pthread_create.
 * It should be returned by your thread so it can be freed by
 * the joiner thread.
 */
struct socket_thread_data
{
    /*
     * add other values your thread will need to manage
     * into this structure, use this structure to communicate
     * between the start_thread_obtaining_mutex function and
     * your thread implementation.
     */
    pthread_mutex_t *mutex;
    int accepted_fd;
    char ip_str[16];

    /**
     * Set to true if the thread completed with success, false
     * if an error occurred.
     */
    bool thread_completed;
    bool thread_generated_error;
};


struct timer_thread_data
{
    pthread_mutex_t *mutex;
};

#endif /* _THREADING_H_ */

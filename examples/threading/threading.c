#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    const int us_per_ms = 1000;
    int rc = 0;
    struct thread_data *thread_arguments = (struct thread_data *)thread_param;
    rc = usleep(thread_arguments->wait_to_obtain_ms * us_per_ms);
    if (rc != 0)
    {
	    return NULL;
    }
    rc = pthread_mutex_lock(thread_arguments->mutex);
    if (rc != 0)
    {
	    return NULL;
    }
    rc = usleep(thread_arguments->wait_to_release_ms * us_per_ms);
    if (rc != 0)
    {
	    return NULL;
    }
    rc = pthread_mutex_unlock(thread_arguments->mutex);
    if (rc != 0)
    {
	    return NULL;
    }
    thread_arguments->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
	int rc = 0;
	struct thread_data *thread_arguments = (struct thread_data *)malloc(sizeof(struct thread_data));
	if (thread_arguments == NULL)
	{
		return false;
	}
	thread_arguments->mutex = mutex;
	thread_arguments->wait_to_obtain_ms = wait_to_obtain_ms;
	thread_arguments->wait_to_release_ms = wait_to_release_ms;
	rc = pthread_create(thread, NULL, threadfunc, (void *)thread_arguments);
	if (rc != 0)
	{
		return false;
	}
	return true;
}


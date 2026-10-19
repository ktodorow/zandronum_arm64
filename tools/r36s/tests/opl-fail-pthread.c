/* Linux negative environment: exercise real std::thread resource failure. */
#include <errno.h>
#include <pthread.h>
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg)
{
    (void)thread; (void)attr; (void)start; (void)arg;
    return EAGAIN;
}

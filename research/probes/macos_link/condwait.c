/*
 * condwait.c: probe M2's watchdog wait (platform/host_thread.h). macOS has no
 * pthread_condattr_setclock, so the wait is pthread_cond_timedwait_relative_np
 * for what is left of a deadline on CLOCK_MONOTONIC, recomputed after every
 * wake-up. A second thread waits for a 300 ms deadline while the first signals
 * it twice for nothing; built plain and with ThreadSanitizer by build.sh, which
 * must report no race and no misuse of the mutex.
 */
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define NANOSECONDS_PER_SECOND 1000000000L
#define DEADLINE_NANOSECONDS 300000000L

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static struct timespec deadline;
static int wakeups;
static bool expired;

static long long nanoseconds(struct timespec t)
{
    return (long long)t.tv_sec * NANOSECONDS_PER_SECOND + t.tv_nsec;
}

static struct timespec monotonic_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now;
}

static void *watch(void *argument)
{
    (void)argument;
    pthread_mutex_lock(&lock);
    for (;;) {
        const long long left = nanoseconds(deadline) - nanoseconds(monotonic_now());
        if (left <= 0) {
            expired = true;
            break;
        }
        const struct timespec relative = {.tv_sec = (time_t)(left / NANOSECONDS_PER_SECOND),
                                          .tv_nsec = (long)(left % NANOSECONDS_PER_SECOND)};
        const int waited = pthread_cond_timedwait_relative_np(&changed, &lock, &relative);
        wakeups++;
        if (waited != 0 && waited != ETIMEDOUT) {
            printf("pthread_cond_timedwait_relative_np: %d\n", waited);
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void)
{
    const struct timespec start = monotonic_now();
    pthread_mutex_lock(&lock);
    deadline = start;
    deadline.tv_nsec += DEADLINE_NANOSECONDS;
    if (deadline.tv_nsec >= NANOSECONDS_PER_SECOND) {
        deadline.tv_sec++;
        deadline.tv_nsec -= NANOSECONDS_PER_SECOND;
    }
    pthread_mutex_unlock(&lock);
    pthread_t thread;
    if (pthread_create(&thread, NULL, watch, NULL) != 0)
        return 1;
    for (int i = 0; i < 2; i++) {
        usleep(50000);
        pthread_mutex_lock(&lock);
        pthread_cond_signal(&changed);
        pthread_mutex_unlock(&lock);
    }
    pthread_join(thread, NULL);
    const long long elapsed = nanoseconds(monotonic_now()) - nanoseconds(start);
    printf("expired %d after %lld ms with %d wake-ups (deadline 300 ms)\n", expired, elapsed / 1000000, wakeups);
    return expired && elapsed >= DEADLINE_NANOSECONDS ? 0 : 1;
}

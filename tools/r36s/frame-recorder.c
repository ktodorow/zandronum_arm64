/* Diagnostic presentation timing; compile on target, never ship in package.
 * cc -O2 -shared -fPIC frame-recorder.c -o frame-recorder.so -ldl
 * Preload only client. SIGUSR1 starts; SIGUSR2 flushes at next presentation.
 * Output: /tmp/zandronum-frames.csv (monotonic nanoseconds).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CAPACITY 65536
static volatile sig_atomic_t request;
static int active, count, overflow;
static uint64_t previous_end, previous_cpu;
static struct frame {
    uint64_t timestamp, interval, work, swap, cpu;
} frames[CAPACITY];

static uint64_t stamp(clockid_t clock)
{
    struct timespec t;
    clock_gettime(clock, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}

static void command(int sig)
{
    request = sig == SIGUSR1 ? 1 : 2;
}

__attribute__((constructor)) static void init(void)
{
    signal(SIGUSR1, command);
    signal(SIGUSR2, command);
}

static void flush(void)
{
    FILE *f = fopen("/tmp/zandronum-frames.csv.tmp", "w");
    if (!f) { perror("frame recorder"); return; }
    fprintf(f, "timestamp_ns,interval_ns,work_ns,swap_ns,cpu_ns\n");
    for (int i = 0; i < count; ++i) {
        const struct frame *p = &frames[i];
        fprintf(f, "%llu,%llu,%llu,%llu,%llu\n",
            (unsigned long long)p->timestamp, (unsigned long long)p->interval,
            (unsigned long long)p->work, (unsigned long long)p->swap,
            (unsigned long long)p->cpu);
    }
    if (overflow) fprintf(f, "overflow\n");
    if (fclose(f)) { perror("frame recorder"); return; }
    if (rename("/tmp/zandronum-frames.csv.tmp", "/tmp/zandronum-frames.csv"))
        perror("frame recorder");
}

static uint64_t begin(void)
{
    if (request == 1) {
        count = overflow = 0;
        active = 1;
        previous_end = 0;
        request = 0;
    } else if (request == 2) {
        active = 0;
        request = 0;
        flush();
    }
    return active ? stamp(CLOCK_MONOTONIC) : 0;
}

static void end(uint64_t start)
{
    if (!active) return;
    uint64_t now = stamp(CLOCK_MONOTONIC);
    uint64_t cpu = stamp(CLOCK_THREAD_CPUTIME_ID);
    if (previous_end) {
        if (count < CAPACITY) {
            frames[count++] = (struct frame){now, now - previous_end,
                start - previous_end, now - start, cpu - previous_cpu};
        } else overflow = 1;
    }
    previous_end = now;
    previous_cpu = cpu;
}

void SDL_GL_SwapBuffers(void)
{
    static void (*next)(void);
    if (!next) next = dlsym(RTLD_NEXT, "SDL_GL_SwapBuffers");
    if (!next) abort();
    uint64_t start = begin();
    next();
    end(start);
}

int SDL_Flip(void *surface)
{
    static int (*next)(void *);
    if (!next) next = dlsym(RTLD_NEXT, "SDL_Flip");
    if (!next) abort();
    uint64_t start = begin();
    int result = next(surface);
    end(start);
    return result;
}

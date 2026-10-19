/* Diagnostic GLES API timing; never include in release packages.
 * cc -O2 -shared -fPIC gles-call-profiler.c -o gles-call-profiler.so -ldl
 * Preload only client, before frame-recorder.so (signal handlers chain):
 * LD_PRELOAD="/tmp/gles-call-profiler.so:/tmp/frame-recorder.so" client ...
 * SIGUSR1 starts at next swap; SIGUSR2 stops and writes at next swap.
 * Fixed output /tmp/zandronum-gles-calls.csv; no disk I/O while recording.
 * Counts/timings are for calls on the GL context's main thread. Driver GPU
 * execution time and physical memory traffic cannot be inferred from them.
 * Independent opt-in probes (unset both for baseline):
 * ZANDRONUM_DIAG_GL_FINISH=1 completes GL before entering inner swap recorder.
 * ZANDRONUM_DIAG_EGL_INTERVAL=0 requests interval0 once current EGL context exists.
 * ZANDRONUM_DIAG_GL_STATE=1 also records draw/vertex-weighted shader-state histogram.
 * See gles-call-profiler.md for interpretation and separate-run examples.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Native API types keep diagnostic build independent of development headers. */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef ptrdiff_t GLintptr, GLsizeiptr;
typedef void *DiagEGLDisplay, *DiagEGLContext, *DiagEGLConfig;
typedef unsigned int DiagEGLBoolean;
typedef int DiagEGLint;

#define CAPACITY 65536
struct metric { uint64_t calls, units, bytes, wall, maximum; };
struct frame {
    uint64_t timestamp, interval, swap;
    struct metric upload, arrays, elements, matrix, storage;
};
static struct frame frames[CAPACITY], current;
static volatile sig_atomic_t request;
static struct sigaction previous_start, previous_stop;
static int active, count, overflow;
static uint64_t previous_end;
static void (*next_upload)(GLenum, GLintptr, GLsizeiptr, const void *);
static void (*next_storage)(GLenum, GLsizeiptr, const void *, GLenum);
static void (*next_arrays)(GLenum, GLint, GLsizei);
static void (*next_elements)(GLenum, GLsizei, GLenum, const void *);
static void (*next_matrix)(GLint, GLsizei, GLboolean, const GLfloat *);
static void (*next_swap)(void);
static void (*real_finish)(void);
static int finish_probe, interval_probe, interval_done;
static void *egl_handle;

#define STATE_PROGRAMS 1024
#define STATE_BUCKETS 8192
struct program_state {
    GLuint program;
    unsigned serial;
    int live, deleted;
    GLint alpha_location, function_location, threshold_location, clip_location;
    GLint fog_location, texture_location, mode_location, material_mode_location, burn_location;
    GLint alpha, function, fog, texture, mode, burn;
    GLfloat threshold;
    unsigned clip_mask;
    char name[96], defines[256];
};
struct state_key {
    GLuint program;
    unsigned serial, primitive, clip_mask, threshold_bits;
    GLint alpha, function, fog, texture, mode, burn;
};
struct state_bucket {
    int used;
    struct state_key key;
    uint64_t draws, vertices, pending_draws, pending_vertices;
    char name[96], defines[256];
};
static struct program_state programs[STATE_PROGRAMS], *current_program;
static struct state_bucket states[STATE_BUCKETS];
static unsigned touched_states[STATE_BUCKETS], touched_count;
static unsigned program_serial;
static int state_probe, state_overflow, program_overflow;
static GLint (*next_location)(GLuint, const char *);
static void (*next_program)(GLuint);
static void (*next_delete_program)(GLuint);
static void (*next_integer)(GLint, GLint);
static void (*next_float)(GLint, GLfloat);
static void (*next_integers)(GLint, GLsizei, const GLint *);

static struct program_state *program_state(GLuint program, int create)
{
    struct program_state *empty = NULL;
    if (!program) return NULL;
    for (unsigned i = 0; i < STATE_PROGRAMS; ++i) {
        if (programs[i].live && programs[i].program == program) return &programs[i];
        if (!programs[i].live && !empty) empty = &programs[i];
    }
    if (!create) return NULL;
    if (!empty) { program_overflow = 1; return NULL; }
    memset(empty, 0, sizeof *empty);
    empty->program = program; empty->live = 1; empty->serial = ++program_serial;
    empty->alpha_location = empty->function_location = empty->threshold_location = empty->clip_location = -1;
    empty->fog_location = empty->texture_location = empty->mode_location = empty->material_mode_location = empty->burn_location = -1;
    return empty;
}

/* Engine labels only programs; draw-state observation remains entirely in this
 * optional preload. Bounded snapshots survive program deletion/name reuse. */
void zandronum_gles_program_label(unsigned program, const char *name, const char *defines)
{
    if (!state_probe) return;
    struct program_state *state = program_state(program, 1);
    if (!state) return;
    snprintf(state->name, sizeof state->name, "%s", name ? name : "");
    snprintf(state->defines, sizeof state->defines, "%s", defines ? defines : "");
}

static void record_state(GLenum primitive, GLsizei vertices)
{
    if (!state_probe || !active || !current_program) return;
    const struct program_state *program = current_program;
    struct state_key key;
    memset(&key, 0, sizeof key);
    key.program = program->program; key.serial = program->serial; key.primitive = primitive;
    key.alpha = program->alpha; key.function = program->function; key.fog = program->fog;
    key.texture = program->texture; key.mode = program->mode; key.burn = program->burn;
    key.clip_mask = program->clip_mask; memcpy(&key.threshold_bits, &program->threshold, sizeof key.threshold_bits);
    uint32_t hash = 2166136261u;
    const unsigned char *bytes = (const unsigned char *)&key;
    for (size_t i = 0; i < sizeof key; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    for (unsigned i = 0; i < STATE_BUCKETS; ++i) {
        struct state_bucket *bucket = &states[(hash + i) % STATE_BUCKETS];
        if (!bucket->used) {
            bucket->used = 1; bucket->key = key;
            memcpy(bucket->name, program->name, sizeof bucket->name);
            memcpy(bucket->defines, program->defines, sizeof bucket->defines);
        } else if (memcmp(&bucket->key, &key, sizeof key)) continue;
        if (!bucket->pending_draws) touched_states[touched_count++] = (hash + i) % STATE_BUCKETS;
        ++bucket->pending_draws; bucket->pending_vertices += vertices > 0 ? (uint64_t)vertices : 0;
        return;
    }
    state_overflow = 1;
}

static void commit_states(int include)
{
    if (!state_probe) return;
    for (unsigned i = 0; i < touched_count; ++i) {
        struct state_bucket *bucket = &states[touched_states[i]];
        if (include) { bucket->draws += bucket->pending_draws; bucket->vertices += bucket->pending_vertices; }
        bucket->pending_draws = bucket->pending_vertices = 0;
    }
    touched_count = 0;
}

static void csv_text(FILE *file, const char *text)
{
    fputc('"', file);
    for (; *text; ++text) {
        if (*text == '"') fputc('"', file);
        fputc(*text == '\r' || *text == '\n' ? ' ' : *text, file);
    }
    fputc('"', file);
}

static void flush_states(void)
{
    if (!state_probe) return;
    FILE *file = fopen("/tmp/zandronum-gles-states.csv.tmp", "w");
    if (!file) { perror("GLES state histogram"); return; }
    fprintf(file, "program,program_serial,material,defines,primitive,draws,submitted_vertices_or_indices,alpha_test,alpha_func,alpha_threshold,alpha_threshold_bits,clip_mask,fog_mode,texture_enabled,texture_mode,burn,overflow\n");
    for (unsigned i = 0; i < STATE_BUCKETS; ++i) {
        const struct state_bucket *bucket = &states[i];
        if (!bucket->used || !bucket->draws) continue;
        const struct state_key *key = &bucket->key;
        GLfloat threshold; memcpy(&threshold, &key->threshold_bits, sizeof threshold);
        fprintf(file, "%u,%u,", key->program, key->serial); csv_text(file, bucket->name);
        fputc(',', file); csv_text(file, bucket->defines);
        fprintf(file, ",%u,%llu,%llu,%d,%d,%.9g,%u,%u,%d,%d,%d,%d,%d\n",
            key->primitive, (unsigned long long)bucket->draws, (unsigned long long)bucket->vertices,
            key->alpha, key->function, (double)threshold, key->threshold_bits, key->clip_mask,
            key->fog, key->texture, key->mode, key->burn, state_overflow || program_overflow);
    }
    if (fclose(file)) { perror("GLES state histogram"); return; }
    if (rename("/tmp/zandronum-gles-states.csv.tmp", "/tmp/zandronum-gles-states.csv")) perror("GLES state histogram");
    if (state_overflow || program_overflow) fprintf(stderr, "GLES state histogram capacity exceeded: states=%d programs=%d\n", state_overflow, program_overflow);
}

static int environment_is(const char *name, const char *expected)
{
    const char *value = getenv(name);
    return value && strcmp(value, expected) == 0;
}

static uint64_t stamp(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000ULL + (uint64_t)time.tv_nsec;
}

static void command(int sig, siginfo_t *info, void *context)
{
    const struct sigaction *previous = sig == SIGUSR1 ? &previous_start : &previous_stop;
    request = sig == SIGUSR1 ? 1 : 2;
    /* Existing frame-recorder's handler only changes its own sig_atomic_t. */
    if (previous->sa_handler != SIG_DFL && previous->sa_handler != SIG_IGN) {
        if (previous->sa_flags & SA_SIGINFO) previous->sa_sigaction(sig, info, context);
        else if (previous->sa_handler) previous->sa_handler(sig);
    }
}

__attribute__((constructor)) static void init(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = command;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR1, &action, &previous_start);
    sigaction(SIGUSR2, &action, &previous_stop);
    *(void **)(&next_upload) = dlsym(RTLD_NEXT, "glBufferSubData");
    *(void **)(&next_storage) = dlsym(RTLD_NEXT, "glBufferData");
    *(void **)(&next_arrays) = dlsym(RTLD_NEXT, "glDrawArrays");
    *(void **)(&next_elements) = dlsym(RTLD_NEXT, "glDrawElements");
    *(void **)(&next_matrix) = dlsym(RTLD_NEXT, "glUniformMatrix4fv");
    *(void **)(&next_swap) = dlsym(RTLD_NEXT, "SDL_GL_SwapBuffers");
    finish_probe = environment_is("ZANDRONUM_DIAG_GL_FINISH", "1");
    interval_probe = environment_is("ZANDRONUM_DIAG_EGL_INTERVAL", "0");
    state_probe = environment_is("ZANDRONUM_DIAG_GL_STATE", "1");
}

static int current_egl(struct dl_phdr_info *info, size_t size, void *unused)
{
    (void)size; (void)unused;
    const char *name = strrchr(info->dlpi_name, '/');
    name = name ? name + 1 : info->dlpi_name;
    if (strncmp(name, "libEGL", 6) != 0) return 0;
    /* SDL may have loaded vendor EGL with RTLD_LOCAL. NOLOAD only acquires
     * an existing object; probing never loads a different EGL implementation. */
    void *handle = dlopen(info->dlpi_name, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) return 0;
    DiagEGLDisplay (*display)(void);
    DiagEGLContext (*context)(void);
    *(void **)(&display) = dlsym(handle, "eglGetCurrentDisplay");
    *(void **)(&context) = dlsym(handle, "eglGetCurrentContext");
    if (display && context && display() && context()) {
        if (egl_handle) dlclose(egl_handle);
        egl_handle = handle;
        return 1;
    }
    if (display && context && !egl_handle) {
        egl_handle = handle; /* Loaded but not current: retry at next swap. */
        return 0;
    }
    dlclose(handle);
    return 0;
}

static void *egl_symbol(const char *name)
{
    return dlsym(egl_handle ? egl_handle : RTLD_DEFAULT, name);
}

static void interval_once(void)
{
    /* Query the already loaded implementation owning the current context.
     * No new library/display/context, EGL/GL dependency, or SDL wrapper. */
    DiagEGLDisplay (*get_display)(void);
    DiagEGLContext (*get_context)(void);
    DiagEGLBoolean (*swap_interval)(DiagEGLDisplay, DiagEGLint);
    DiagEGLint (*get_error)(void);
    DiagEGLBoolean (*query_context)(DiagEGLDisplay, DiagEGLContext, DiagEGLint, DiagEGLint *);
    DiagEGLBoolean (*choose_config)(DiagEGLDisplay, const DiagEGLint *, DiagEGLConfig *, DiagEGLint, DiagEGLint *);
    DiagEGLBoolean (*config_attribute)(DiagEGLDisplay, DiagEGLConfig, DiagEGLint, DiagEGLint *);
    dl_iterate_phdr(current_egl, NULL);
    *(void **)(&get_display) = egl_symbol("eglGetCurrentDisplay");
    *(void **)(&get_context) = egl_symbol("eglGetCurrentContext");
    *(void **)(&swap_interval) = egl_symbol("eglSwapInterval");
    *(void **)(&get_error) = egl_symbol("eglGetError");
    if (!get_display || !get_context || !swap_interval || !get_error) {
        fprintf(stderr, "GLES diagnostic: interval0 unavailable: loaded EGL entrypoints missing\n");
        interval_done = 1;
        return;
    }
    DiagEGLDisplay display = get_display();
    DiagEGLContext context = get_context();
    if (!display || !context) return; /* Retry when a real context is current. */
    interval_done = 1;
    DiagEGLint previous_error = get_error();
    DiagEGLBoolean result = swap_interval(display, 0);
    DiagEGLint error = get_error();
    DiagEGLint minimum = -1, maximum = -1, id = 0, count = 0;
    DiagEGLConfig config = NULL;
    *(void **)(&query_context) = egl_symbol("eglQueryContext");
    *(void **)(&choose_config) = egl_symbol("eglChooseConfig");
    *(void **)(&config_attribute) = egl_symbol("eglGetConfigAttrib");
    if (query_context && choose_config && config_attribute
        && query_context(display, context, 0x3028 /* EGL_CONFIG_ID */, &id)) {
        const DiagEGLint attributes[] = { 0x3028, id, 0x3038 /* EGL_NONE */ };
        if (choose_config(display, attributes, &config, 1, &count) && count > 0) {
            if (!config_attribute(display, config, 0x303b /* EGL_MIN_SWAP_INTERVAL */, &minimum)) minimum = -1;
            if (!config_attribute(display, config, 0x303c /* EGL_MAX_SWAP_INTERVAL */, &maximum)) maximum = -1;
        }
    }
    DiagEGLint query_error = get_error();
    fprintf(stderr, "GLES diagnostic: eglSwapInterval(0) result=%u error=0x%x prior_error=0x%x min=%d max=%d query_error=0x%x\n",
        result, (unsigned)error, (unsigned)previous_error, minimum, maximum, (unsigned)query_error);
}

static void before_swap(void)
{
    if (interval_probe && !interval_done) interval_once();
    if (finish_probe) {
        if (!real_finish) {
            *(void **)(&real_finish) = dlsym(RTLD_NEXT, "glFinish");
            if (!real_finish) {
                fprintf(stderr, "GLES diagnostic: finish probe unavailable: loaded glFinish missing\n");
                finish_probe = 0;
                return;
            }
            fprintf(stderr, "GLES diagnostic: glFinish before inner swap recorder enabled\n");
        }
        real_finish();
    }
}

static void add(struct metric *metric, uint64_t units, uint64_t bytes, uint64_t start)
{
    uint64_t elapsed = stamp() - start;
    ++metric->calls;
    metric->units += units;
    metric->bytes += bytes;
    metric->wall += elapsed;
    if (elapsed > metric->maximum) metric->maximum = elapsed;
}

static void write_metric(FILE *file, const struct metric *metric)
{
    fprintf(file, ",%llu,%llu,%llu,%llu,%llu",
        (unsigned long long)metric->calls, (unsigned long long)metric->units,
        (unsigned long long)metric->bytes, (unsigned long long)metric->wall,
        (unsigned long long)metric->maximum);
}

static void flush(void)
{
    FILE *file = fopen("/tmp/zandronum-gles-calls.csv.tmp", "w");
    if (!file) { perror("GLES call profiler"); return; }
    fprintf(file, "timestamp_ns,interval_ns,swap_ns,"
        "upload_calls,upload_units_bytes,upload_bytes,upload_wall_ns,upload_max_ns,"
        "draw_arrays_calls,draw_arrays_vertices,draw_arrays_bytes_unknown,draw_arrays_wall_ns,draw_arrays_max_ns,"
        "draw_elements_calls,draw_elements_indices,draw_elements_index_bytes,draw_elements_wall_ns,draw_elements_max_ns,"
        "matrix4_calls,matrix4_matrices,matrix4_bytes,matrix4_wall_ns,matrix4_max_ns,"
        "buffer_data_calls,buffer_data_allocated_bytes,buffer_data_source_bytes,buffer_data_wall_ns,buffer_data_max_ns\n");
    for (int i = 0; i < count; ++i) {
        const struct frame *frame = &frames[i];
        fprintf(file, "%llu,%llu,%llu", (unsigned long long)frame->timestamp,
            (unsigned long long)frame->interval, (unsigned long long)frame->swap);
        write_metric(file, &frame->upload); write_metric(file, &frame->arrays);
        write_metric(file, &frame->elements); write_metric(file, &frame->matrix);
        write_metric(file, &frame->storage);
        fputc('\n', file);
    }
    if (overflow) fprintf(file, "overflow\n");
    if (fclose(file)) { perror("GLES call profiler"); return; }
    if (rename("/tmp/zandronum-gles-calls.csv.tmp", "/tmp/zandronum-gles-calls.csv"))
        perror("GLES call profiler");
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    if (!next_upload) { *(void **)(&next_upload) = dlsym(RTLD_NEXT, "glBufferSubData"); if (!next_upload) abort(); }
    uint64_t start = active ? stamp() : 0;
    next_upload(target, offset, size, data);
    if (start) add(&current.upload, size > 0 ? (uint64_t)size : 0, size > 0 ? (uint64_t)size : 0, start);
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    if (!next_storage) { *(void **)(&next_storage) = dlsym(RTLD_NEXT, "glBufferData"); if (!next_storage) abort(); }
    uint64_t start = active ? stamp() : 0;
    next_storage(target, size, data, usage);
    uint64_t bytes = size > 0 ? (uint64_t)size : 0;
    /* Null source allocates storage without supplying bytes from client memory. */
    if (start) add(&current.storage, bytes, data ? bytes : 0, start);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei vertices)
{
    if (!next_arrays) { *(void **)(&next_arrays) = dlsym(RTLD_NEXT, "glDrawArrays"); if (!next_arrays) abort(); }
    uint64_t start = active ? stamp() : 0;
    next_arrays(mode, first, vertices);
    /* Attribute strides and buffers differ; byte volume is unknown, not zero traffic. */
    if (start) add(&current.arrays, vertices > 0 ? (uint64_t)vertices : 0, 0, start);
    record_state(mode, vertices);
}

void glDrawElements(GLenum mode, GLsizei indices, GLenum type, const void *pointer)
{
    if (!next_elements) { *(void **)(&next_elements) = dlsym(RTLD_NEXT, "glDrawElements"); if (!next_elements) abort(); }
    uint64_t start = active ? stamp() : 0;
    next_elements(mode, indices, type, pointer);
    unsigned unit = type == 0x1401 ? 1 : type == 0x1403 ? 2 : type == 0x1405 ? 4 : 0;
    uint64_t units = indices > 0 ? (uint64_t)indices : 0;
    if (start) add(&current.elements, units, units * unit, start);
    record_state(mode, indices);
}

GLint glGetUniformLocation(GLuint program, const char *name)
{
    if (!next_location) { *(void **)(&next_location) = dlsym(RTLD_NEXT, "glGetUniformLocation"); if (!next_location) abort(); }
    GLint location = next_location(program, name);
    if (state_probe && location >= 0) {
        struct program_state *state = program_state(program, 1);
        if (state) {
            if (!strcmp(name, "uAlphaTest")) state->alpha_location = location;
            else if (!strcmp(name, "uAlphaFunc")) state->function_location = location;
            else if (!strcmp(name, "uAlphaThreshold")) state->threshold_location = location;
            else if (!strcmp(name, "uClipEnabled[0]")) state->clip_location = location;
            else if (!strcmp(name, "fogenabled")) state->fog_location = location;
            else if (!strcmp(name, "uTextureEnabled")) state->texture_location = location;
            else if (!strcmp(name, "uTextureMode")) state->mode_location = location;
            else if (!strcmp(name, "texturemode")) state->material_mode_location = location;
            else if (!strcmp(name, "uBurn")) state->burn_location = location;
        }
    }
    return location;
}

void glUseProgram(GLuint program)
{
    if (!next_program) { *(void **)(&next_program) = dlsym(RTLD_NEXT, "glUseProgram"); if (!next_program) abort(); }
    next_program(program);
    if (state_probe) {
        struct program_state *previous = current_program;
        current_program = program_state(program, 1);
        if (previous && previous != current_program && previous->deleted) memset(previous, 0, sizeof *previous);
    }
}

void glDeleteProgram(GLuint program)
{
    if (!next_delete_program) { *(void **)(&next_delete_program) = dlsym(RTLD_NEXT, "glDeleteProgram"); if (!next_delete_program) abort(); }
    next_delete_program(program);
    if (state_probe) {
        struct program_state *state = program_state(program, 0);
        if (state == current_program && state) state->deleted = 1;
        else if (state) memset(state, 0, sizeof *state);
    }
}

void glUniform1i(GLint location, GLint value)
{
    if (!next_integer) { *(void **)(&next_integer) = dlsym(RTLD_NEXT, "glUniform1i"); if (!next_integer) abort(); }
    next_integer(location, value);
    struct program_state *state = current_program;
    if (!state_probe || !state || location < 0) return;
    if (location == state->alpha_location) state->alpha = value;
    if (location == state->function_location) state->function = value;
    if (location == state->fog_location) state->fog = value;
    if (location == state->texture_location) state->texture = value;
    if (location == state->mode_location || location == state->material_mode_location) state->mode = value;
    if (location == state->burn_location) state->burn = value;
}

void glUniform1f(GLint location, GLfloat value)
{
    if (!next_float) { *(void **)(&next_float) = dlsym(RTLD_NEXT, "glUniform1f"); if (!next_float) abort(); }
    next_float(location, value);
    if (state_probe && current_program && location >= 0 && location == current_program->threshold_location) current_program->threshold = value;
}

void glUniform1iv(GLint location, GLsizei values, const GLint *value)
{
    if (!next_integers) { *(void **)(&next_integers) = dlsym(RTLD_NEXT, "glUniform1iv"); if (!next_integers) abort(); }
    next_integers(location, values, value);
    if (state_probe && current_program && location >= 0 && location == current_program->clip_location) {
        for (GLsizei i = 0; i < values && i < 6; ++i) {
            unsigned bit = 1u << (unsigned)i;
            if (value[i]) current_program->clip_mask |= bit;
            else current_program->clip_mask &= ~bit;
        }
    }
}

void glUniformMatrix4fv(GLint location, GLsizei matrices, GLboolean transpose, const GLfloat *value)
{
    if (!next_matrix) { *(void **)(&next_matrix) = dlsym(RTLD_NEXT, "glUniformMatrix4fv"); if (!next_matrix) abort(); }
    uint64_t start = active ? stamp() : 0;
    next_matrix(location, matrices, transpose, value);
    uint64_t units = matrices > 0 ? (uint64_t)matrices : 0;
    if (start) add(&current.matrix, units, units * 16 * sizeof(GLfloat), start);
}

void SDL_GL_SwapBuffers(void)
{
    if (!next_swap) { *(void **)(&next_swap) = dlsym(RTLD_NEXT, "SDL_GL_SwapBuffers"); if (!next_swap) abort(); }
    if (request == 1) {
        count = overflow = 0; active = 1; previous_end = 0;
        if (state_probe) { memset(states, 0, sizeof states); touched_count = 0; state_overflow = 0; }
        memset(&current, 0, sizeof current); request = 0;
    } else if (request == 2) {
        active = 0; request = 0; commit_states(0); flush(); flush_states();
    }
    before_swap();
    uint64_t start = active ? stamp() : 0;
    next_swap();
    if (active) {
        uint64_t end = stamp();
        if (previous_end) {
            current.timestamp = end; current.interval = end - previous_end; current.swap = end - start;
            if (count < CAPACITY) frames[count++] = current;
            else overflow = 1;
        }
        commit_states(previous_end != 0);
        previous_end = end;
        memset(&current, 0, sizeof current);
    }
}

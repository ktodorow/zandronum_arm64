/* Optional one-shot GLES driver inventory. Never include in release packages.
 * cc -O2 -shared -fPIC gles-extension-dump.c -o gles-extension-dump.so -ldl
 * LD_PRELOAD=/tmp/gles-extension-dump.so client ...
 * First SDL swap with a current context writes GL strings and extensions to
 * /tmp/zandronum-gles-extensions.txt (or ZANDRONUM_GLES_EXTENSION_LOG).
 * No GL state is changed. Later swaps only forward to the next interposer.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef const unsigned char *(*GetString)(unsigned int);
typedef void *(*GetProcAddress)(const char *);
static void (*next_swap)(void);
static GetString get_string;
static int attempted;

__attribute__((constructor)) static void init(void)
{
    *(void **)(&next_swap) = dlsym(RTLD_NEXT, "SDL_GL_SwapBuffers");
}

void SDL_GL_SwapBuffers(void)
{
    if (!attempted && !get_string) {
        GetProcAddress get_proc;
        *(void **)(&get_string) = dlsym(RTLD_DEFAULT, "glGetString");
        *(void **)(&get_proc) = dlsym(RTLD_DEFAULT, "SDL_GL_GetProcAddress");
        if (!get_string && get_proc) *(void **)(&get_string) = get_proc("glGetString");
    }
    if (!attempted && get_string) {
        const unsigned char *version = get_string(0x1f02); /* GL_VERSION */
        if (version) {
            const char *path = getenv("ZANDRONUM_GLES_EXTENSION_LOG");
            int fd;
            FILE *out;
            attempted = 1;
            if (!path || !*path) path = "/tmp/zandronum-gles-extensions.txt";
            fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
            out = fd < 0 ? NULL : fdopen(fd, "w");
            if (out) {
                const unsigned int names[] = {0x1f00, 0x1f01, 0x1f02, 0x8b8c, 0x1f03};
                const char *labels[] = {"GL_VENDOR", "GL_RENDERER", "GL_VERSION",
                                        "GL_SHADING_LANGUAGE_VERSION", "GL_EXTENSIONS"};
                unsigned int i;
                for (i = 0; i < sizeof names / sizeof names[0]; ++i) {
                    const unsigned char *value = get_string(names[i]);
                    fprintf(out, "%s=%s\n", labels[i], value ? (const char *)value : "(null)");
                }
                fclose(out);
            } else {
                if (fd >= 0) close(fd);
                fprintf(stderr, "GLES extension inventory: cannot write %s\n", path);
            }
        }
    }
    if (next_swap) next_swap();
}

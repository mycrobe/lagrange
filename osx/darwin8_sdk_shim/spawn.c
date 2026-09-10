/*
 * darwin8_sdk_shim/spawn.c -- implementation of the posix_spawn shim (see spawn.h).
 * Tiger lacks posix_spawn()/posix_spawn_file_actions_*() (10.4u SDK).  Implemented
 * over fork()+execve() for the subset the_Foundation's process.c uses: a handful
 * of close/dup2 file actions, then execve().  Returns the POSIX error code
 * (0 on success) as the real APIs do, not -1.
 */
#include "spawn.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

enum { OP_CLOSE = 0, OP_DUP2 = 1 };

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *actions) {
    if (!actions) return EINVAL;
    actions->count = 0;
    return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *actions) {
    if (!actions) return EINVAL;
    actions->count = 0;
    return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *actions, int fd) {
    if (!actions || actions->count >= (int) (sizeof actions->acts / sizeof actions->acts[0])) {
        return EINVAL;
    }
    actions->acts[actions->count].op  = OP_CLOSE;
    actions->acts[actions->count].fd1 = fd;
    actions->acts[actions->count].fd2 = -1;
    actions->count++;
    return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *actions, int fd, int newfd) {
    if (!actions || actions->count >= (int) (sizeof actions->acts / sizeof actions->acts[0])) {
        return EINVAL;
    }
    actions->acts[actions->count].op  = OP_DUP2;
    actions->acts[actions->count].fd1 = fd;
    actions->acts[actions->count].fd2 = newfd;
    actions->count++;
    return 0;
}

int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *file_actions,
                const void *attrp, char *const argv[], char *const envp[]) {
    /* The attribute pointer is unused (Tiger lacks posix_spawnattr_t). */
    (void) attrp;
    if (!path || !argv || !argv[0]) return EINVAL;

    const pid_t child = fork();
    if (child < 0) {
        return errno;
    }
    if (child == 0) {
        /* Child: apply the file actions, then exec.  This is the same order as
           the real posix_spawn (file actions run before exec). */
        if (file_actions) {
            for (int i = 0; i < file_actions->count; i++) {
                if (file_actions->acts[i].op == OP_CLOSE) {
                    close(file_actions->acts[i].fd1);
                }
                else {
                    dup2(file_actions->acts[i].fd1, file_actions->acts[i].fd2);
                }
            }
        }
        execve(path, argv, envp);
        _exit(127); /* exec failed */
    }
    if (pid) *pid = child;
    return 0;
}

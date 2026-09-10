/*
 * darwin8_sdk_shim/spawn.h -- minimal posix_spawn shim for Tiger (10.4u SDK).
 * Tiger has no <spawn.h>.  the_Foundation's platform/posix/process.c includes it
 * to start child processes through posix_spawn()/posix_spawn_file_actions_*().
 * We provide the real thing over fork()+execve() so iProcess actually works on
 * the target (a declaration-only stub would silently break the full app when it
 * later uses iProcess).  Only the subset process.c uses is implemented.
 *
 * Scoped PRIVATE to the darwin8 the_Foundation build; migrated into the
 * the_Foundation apple platform layer in a future commit (docs/arcana.md).
 */
#ifndef DARWIN8_SPAWN_SHIM_H
#define DARWIN8_SPAWN_SHIM_H

#include <sys/types.h>
#include <unistd.h>

typedef struct posix_spawn_file_actions_t {
    int    count;
    struct {
        int op;   /* 0 = close, 1 = dup2 */
        int fd1;
        int fd2;
    } acts[16];
} posix_spawn_file_actions_t;

#ifdef __cplusplus
extern "C" {
#endif

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *actions);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *actions);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *actions, int fd);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *actions, int fd, int newfd);

int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *file_actions,
                const void *attrp, char *const argv[], char *const envp[]);

#ifdef __cplusplus
}
#endif

#endif /* DARWIN8_SPAWN_SHIM_H */

#include "cuda/cuda_runtime_info.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/wait.h>

static int is_truthy_env(const char *v)
{
    if (!v || !v[0]) return 0;
    if (strcmp(v, "1") == 0) return 1;
    if (strcasecmp(v, "true") == 0) return 1;
    if (strcasecmp(v, "yes") == 0) return 1;
    if (strcasecmp(v, "on") == 0) return 1;
    return 0;
}

void cuda_runtime_apply_safe_env(void)
{
    const char *force_disable_jit = getenv("BDS_CUDA_DISABLE_JIT");
    if (is_truthy_env(force_disable_jit)) {
        setenv("CUDA_DISABLE_PTX_JIT", "1", 1);
        setenv("CUDA_FORCE_PTX_JIT", "0", 1);
        setenv("CUDA_DISABLE_JIT", "1", 1);
    } else {
        setenv("CUDA_DISABLE_PTX_JIT", "0", 1);
        setenv("CUDA_FORCE_PTX_JIT", "0", 1);
        setenv("CUDA_DISABLE_JIT", "0", 1);
    }
    setenv("CUDA_MODULE_LOADING", "EAGER", 1);
}

int cuda_runtime_is_enabled_by_env(void)
{
    const char *disable = getenv("BDS_DISABLE_CUDA");
    return is_truthy_env(disable) ? 0 : 1;
}

int cuda_runtime_should_run_smoke(void)
{
    const char *skip = getenv("BDS_SKIP_CUDA_SMOKE");
    return is_truthy_env(skip) ? 0 : 1;
}

int cuda_runtime_probe_safely(int (*smoke_test_fn)(void))
{
    if (!smoke_test_fn) return 0;

    pid_t pid = fork();
    if (pid < 0) return 0;

    if (pid == 0) {
        int ok = smoke_test_fn();
        _exit(ok ? 0 : 3);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 0;
    if (WIFSIGNALED(status)) return 0;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return 1;
    return 0;
}

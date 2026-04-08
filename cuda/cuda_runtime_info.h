#ifndef CUDA_RUNTIME_INFO_H
#define CUDA_RUNTIME_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

void cuda_runtime_apply_safe_env(void);
int cuda_runtime_is_enabled_by_env(void);
int cuda_runtime_should_run_smoke(void);
int cuda_runtime_probe_safely(int (*smoke_test_fn)(void));

#ifdef __cplusplus
}
#endif

#endif

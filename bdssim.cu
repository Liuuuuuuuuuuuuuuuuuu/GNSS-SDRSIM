/* bdssim.c : BeiDou B1I baseband generator */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <omp.h>
#include <cuda_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "bdssim.h"
#include "coord.h"
#include "orbits.h"
#include "navbits.h"
#include "timeconv.h"
#include "usrp_wrapper.h"
#include "path.h"
#include "iono.h"

#ifdef __cplusplus
}
#endif

#define FSAMP_DEF  FS_OUTPUT_HZ    /* default 5.2 MHz for 16-bit I/Q output */
#define FSAMP_BYTE FS_BYTE_HZ      /* 25 MHz when --byte is used */
#define IF_BYTE    0.0       /* baseband (0 Hz IF) for --byte output */
#define CUDA_PI2 6.2831853071795864769
#define CUDA_CODE_LEN 2046 // 北斗 B1I 碼長
#define CUDA_GPS_PRN_MAX 32

typedef struct {
    bool valid;
    int n_ch;
    channel_t ch[MAX_CH];
    double fs;
    int single_prn;
    bool meo_only;
    bool prn37_only;
    uint8_t signal_mode;
    double next_bdt;
} signal_engine_state_t;

static signal_engine_state_t g_sig_state = {0};
static bool g_prn_tables_ready = false;

static void ensure_prn_tables_ready(void)
{
    if (g_prn_tables_ready) return;
    init_prn_tables();
    g_prn_tables_ready = true;
}

void reset_signal_engine_state(void)
{
    memset(&g_sig_state, 0, sizeof(g_sig_state));
}

extern "C" int cuda_runtime_smoke_test(void)
{
    cudaError_t st = cudaFree(0);
    return (st == cudaSuccess) ? 1 : 0;
}

/* Lightweight PRN jammer channel state used by GPU interference synthesis. */
typedef struct {
    int16_t prn;
    uint8_t is_gps;
    int8_t nav_sign;
    float amp;
    float if_hz;
    float doppler_hz;
    float carr_phase0;
    float code_phase0;
    float chip_rate;
} jam_prn_state_t;

__device__ int16_t d_ca_wave[64][CUDA_CODE_LEN];
__device__ int16_t d_gps_ca_wave[33][CUDA_CODE_LEN];
extern __global__ void gpu_generate_jam_prn_kernel(
    const jam_prn_state_t *d_jam_ch,
    int n_jam_ch,
    int samp_per_ms,
    double fs,
    int16_t *d_iq_out);
extern __global__ void gpu_spectrum_bins_kernel(
    const int16_t* d_iq_out,
    int n_fft,
    int bins,
    float* d_out_db);

static inline uint32_t host_mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline int jam_pseudo_nav_sign(uint32_t seed, int prn, int is_gps, int nav_bit_idx)
{
    /* Repeat every 300 bits (~6 s) to mimic structured nav data cadence. */
    int frame_bit = nav_bit_idx % 300;
    uint32_t key = seed
                 ^ ((uint32_t)prn * 0x9e3779b9U)
                 ^ ((uint32_t)is_gps * 0x85ebca6bU)
                 ^ ((uint32_t)frame_bit * 0x27d4eb2dU);
    return (host_mix32(key) & 1U) ? 1 : -1;
}

static void generate_interference_signal(const sim_config_t *cfg,
                                         path_t *path,
                                         const coord_t *usr0,
                                         const coord_t *ref_llh,
                                         double start_bdt)
{
    ensure_prn_tables_ready();

    const double fs = cfg->fs;
    const int samp_per_ms = (int)(fs / 1000.0 + 0.5);
    const int step_ms = cfg->step_ms;
    const uint64_t total_ms = (uint64_t)cfg->duration * 1000;
    const size_t iq_bytes = (size_t)(2 * samp_per_ms) * sizeof(int16_t);
    int16_t *host_iq = (int16_t *)malloc(iq_bytes);
    int16_t *d_iq_out = NULL;
    jam_prn_state_t *d_jam_ch = NULL;
    float *d_spec_db = NULL;
    if (!host_iq) {
        fputs("interference alloc error\n", stderr);
        return;
    }

    const bool use_bds = (cfg->signal_mode == SIG_MODE_BDS || cfg->signal_mode == SIG_MODE_MIXED);
    const bool use_gps = (cfg->signal_mode == SIG_MODE_GPS || cfg->signal_mode == SIG_MODE_MIXED);

    /* Ensure real PRN code tables are present in GPU memory. */
    int16_t local_ca[64][CUDA_CODE_LEN];
    int16_t local_gps_ca[33][CUDA_CODE_LEN];
    for (int p = 1; p <= 63; ++p) {
        for (int i = 0; i < CUDA_CODE_LEN; ++i) {
            local_ca[p][i] = prn_code[p][i] ? 1 : -1;
        }
    }
    for (int p = 1; p <= CUDA_GPS_PRN_MAX; ++p) {
        for (int i = 0; i < CUDA_CODE_LEN; ++i) {
            int src = i >> 1;
            local_gps_ca[p][i] = gps_prn_code[p][src] ? 1 : -1;
        }
    }
    cudaMemcpyToSymbol(d_ca_wave, local_ca, sizeof(local_ca));
    cudaMemcpyToSymbol(d_gps_ca_wave, local_gps_ca, sizeof(local_gps_ca));

    enum { JAM_MAX_PRN = 95 };
    jam_prn_state_t jam_ch[JAM_MAX_PRN];
    int n_jam_ch = 0;

    const int total_cfg_ch = (cfg->max_ch > 0) ? cfg->max_ch : 16;
    int target_total = total_cfg_ch;
    if (target_total < 1) target_total = 1;
    if (target_total > JAM_MAX_PRN) target_total = JAM_MAX_PRN;

    int bds_prn_count = 0;
    int gps_prn_count = 0;
    if (use_bds && use_gps) {
        bds_prn_count = (int)llround((double)target_total * (63.0 / (63.0 + (double)CUDA_GPS_PRN_MAX)));
        if (bds_prn_count < 1) bds_prn_count = 1;
        if (bds_prn_count > target_total - 1) bds_prn_count = target_total - 1;
        gps_prn_count = target_total - bds_prn_count;
    } else if (use_bds) {
        bds_prn_count = target_total;
    } else if (use_gps) {
        gps_prn_count = target_total;
    }
    if (bds_prn_count > 63) bds_prn_count = 63;
    if (gps_prn_count > CUDA_GPS_PRN_MAX) gps_prn_count = CUDA_GPS_PRN_MAX;

    /* Back off total jammer power to prevent heavy clipping after all-PRN summation. */
    double amp_base = cfg->gain * 13000.0 * JAM_AMP_SCALE;
    if (amp_base < 7000.0) amp_base = 7000.0;
    if (amp_base > 17000.0) amp_base = 17000.0;
    double amp_bds = use_bds ? amp_base : 0.0;
    double amp_gps = use_gps ? amp_base : 0.0;
    if (use_bds && use_gps) {
        amp_bds *= 0.62;
        amp_gps *= 0.62;
    }

    const double per_ch_backoff = (use_bds && use_gps) ? 0.65 : 0.80;
    double bds_amp_per_prn = (bds_prn_count > 0) ? (amp_bds / sqrt((double)bds_prn_count) * per_ch_backoff) : 0.0;
    double gps_amp_per_prn = (gps_prn_count > 0) ? (amp_gps / sqrt((double)gps_prn_count) * per_ch_backoff) : 0.0;

    uint32_t seed = cfg->seed ? (uint32_t)cfg->seed : 1u;

    if (use_bds) {
        uint8_t used[64] = {0};
        for (int k = 0; k < bds_prn_count && n_jam_ch < JAM_MAX_PRN; ++k) {
            uint32_t h = host_mix32(seed ^ (uint32_t)((k + 1) * 0x9e3779b9U));
            int prn = 1 + (int)(h % 63U);
            while (used[prn]) {
                prn += 1;
                if (prn > 63) prn = 1;
            }
            used[prn] = 1;
            jam_ch[n_jam_ch].prn = (int16_t)prn;
            jam_ch[n_jam_ch].is_gps = 0;
            jam_ch[n_jam_ch].nav_sign = 1;
            jam_ch[n_jam_ch].amp = (float)bds_amp_per_prn;
            jam_ch[n_jam_ch].if_hz = (float)g_bds_if_offset_hz;
            jam_ch[n_jam_ch].doppler_hz = (float)(((int)(h % 5001U)) - 2500); /* +/-2.5kHz */
            jam_ch[n_jam_ch].carr_phase0 = (float)(CUDA_PI2 * ((double)(h & 0xFFFFU) / 65535.0));
            jam_ch[n_jam_ch].code_phase0 = (float)(h % CUDA_CODE_LEN);
            jam_ch[n_jam_ch].chip_rate = (float)CHIPRATE;
            ++n_jam_ch;
        }
    }
    if (use_gps) {
        uint8_t used[CUDA_GPS_PRN_MAX + 1] = {0};
        for (int k = 0; k < gps_prn_count && n_jam_ch < JAM_MAX_PRN; ++k) {
            uint32_t h = host_mix32((seed ^ 0x85ebca6bU) ^ (uint32_t)((k + 1) * 0x27d4eb2dU));
            int prn = 1 + (int)(h % (uint32_t)CUDA_GPS_PRN_MAX);
            while (used[prn]) {
                prn += 1;
                if (prn > CUDA_GPS_PRN_MAX) prn = 1;
            }
            used[prn] = 1;
            jam_ch[n_jam_ch].prn = (int16_t)prn;
            jam_ch[n_jam_ch].is_gps = 1;
            jam_ch[n_jam_ch].nav_sign = 1;
            jam_ch[n_jam_ch].amp = (float)gps_amp_per_prn;
            jam_ch[n_jam_ch].if_hz = (float)g_gps_if_offset_hz;
            jam_ch[n_jam_ch].doppler_hz = (float)(((int)(h % 5001U)) - 2500); /* +/-2.5kHz */
            jam_ch[n_jam_ch].carr_phase0 = (float)(CUDA_PI2 * ((double)(h & 0xFFFFU) / 65535.0));
            jam_ch[n_jam_ch].code_phase0 = (float)(h % CUDA_CODE_LEN);
            /* d_gps_ca_wave is expanded to 2046 entries, so use CODE_LEN-scale chip rate. */
            jam_ch[n_jam_ch].chip_rate = (float)CHIPRATE;
            ++n_jam_ch;
        }
    }

    if (n_jam_ch <= 0) {
        free(host_iq);
        return;
    }

    cudaMalloc((void **)&d_iq_out, iq_bytes);
    cudaMalloc((void **)&d_jam_ch, (size_t)n_jam_ch * sizeof(jam_prn_state_t));
    cudaMalloc((void **)&d_spec_db, (size_t)GUI_SPECTRUM_BINS * sizeof(float));
    cudaMemcpy(d_jam_ch, jam_ch, (size_t)n_jam_ch * sizeof(jam_prn_state_t), cudaMemcpyHostToDevice);

    uint64_t sample_count = 0;
    uint64_t jam_ms_index = 0;
    float spec_host[GUI_SPECTRUM_BINS] = {0.0f};
    uint64_t spec_sample_acc = 0;
    uint64_t spec_period_samples = (uint64_t)(fs / (double)GUI_SPEC_FPS + 0.5);
    if (spec_period_samples < (uint64_t)samp_per_ms) spec_period_samples = (uint64_t)samp_per_ms;
    int spec_fft = GUI_SPEC_FFT_POINTS;
    if (spec_fft > samp_per_ms) spec_fft = samp_per_ms;
    if (spec_fft < 64) spec_fft = 64;

    cudaStream_t spec_stream;
    cudaEvent_t spec_done_evt;
    cudaStreamCreateWithFlags(&spec_stream, cudaStreamNonBlocking);
    cudaEventCreateWithFlags(&spec_done_evt, cudaEventDisableTiming);
    bool spec_inflight = false;
    int last_nav_bit_idx = -1;

    for (uint64_t ms = 0; ms < total_ms; ms += step_ms) {
        if (g_runtime_abort) break;

        for (int step = 0; step < step_ms; ++step) {
            if (g_runtime_abort) break;

            g_t_tx = start_bdt + sample_count / fs;
            int week = (int)(g_t_tx / 604800.0);
            double sow = g_t_tx - week * 604800.0;

            coord_t usr = *usr0;
            if (cfg->path_type == 0) {
                static_user_at(week, sow, ref_llh, &usr, NULL);
            } else if (cfg->path_type == 1 || cfg->path_type == 2) {
                const int is_llh = (cfg->path_type == 2) ? 1 : 0;
                double dummy_vel[3] = {0.0, 0.0, 0.0};
                interpolate_path_kinematic(path, (double)(ms + (uint64_t)step) / 1000.0,
                                           &usr, dummy_vel, is_llh);
                if (!is_llh) ecef_to_lla(usr.xyz, &usr);
                usr.week = week;
                usr.sow = sow;
            } else {
                double llh[3];
                interpolate_path_llh(path, (ms + step) / 1000.0, llh);
                coord_t ref = {0};
                ref.llh[0] = llh[0];
                ref.llh[1] = llh[1];
                ref.llh[2] = llh[2];
                static_user_at(week, sow, &ref, &usr, NULL);
            }

            g_receiver_lat_deg = usr.llh[0] * 180.0 / M_PI;
            g_receiver_lon_deg = usr.llh[1] * 180.0 / M_PI;
            g_receiver_valid = 1;

            {
                int threadsPerBlock = 256;
                int blocksPerGrid = (samp_per_ms + threadsPerBlock - 1) / threadsPerBlock;
                int nav_bit_idx = (int)(jam_ms_index / 20ULL);
                if (nav_bit_idx != last_nav_bit_idx) {
                    last_nav_bit_idx = nav_bit_idx;
                    for (int i = 0; i < n_jam_ch; ++i) {
                        jam_ch[i].nav_sign = (int8_t)jam_pseudo_nav_sign(
                            seed,
                            (int)jam_ch[i].prn,
                            (int)jam_ch[i].is_gps,
                            nav_bit_idx);
                    }
                }

                /* Keep phase/code phase continuous by advancing channel state every ms on host. */
                cudaMemcpy(d_jam_ch, jam_ch,
                           (size_t)n_jam_ch * sizeof(jam_prn_state_t),
                           cudaMemcpyHostToDevice);

                gpu_generate_jam_prn_kernel<<<blocksPerGrid, threadsPerBlock>>>(
                    d_jam_ch, n_jam_ch, samp_per_ms, fs, d_iq_out);
                cudaMemcpy(host_iq, d_iq_out, iq_bytes, cudaMemcpyDeviceToHost);

                for (int i = 0; i < n_jam_ch; ++i) {
                    const double carr_hz = (double)jam_ch[i].if_hz + (double)jam_ch[i].doppler_hz;
                    double ph = (double)jam_ch[i].carr_phase0 + CUDA_PI2 * carr_hz * 0.001;
                    ph = fmod(ph, CUDA_PI2);
                    if (ph < 0.0) ph += CUDA_PI2;
                    jam_ch[i].carr_phase0 = (float)ph;

                    double cp = (double)jam_ch[i].code_phase0 + (double)jam_ch[i].chip_rate * 0.001;
                    cp = fmod(cp, (double)CUDA_CODE_LEN);
                    if (cp < 0.0) cp += (double)CUDA_CODE_LEN;
                    jam_ch[i].code_phase0 = (float)cp;
                }
            }

            /* Spectrum update every spec_period_samples */
            spec_sample_acc += (uint64_t)samp_per_ms;

            if (spec_inflight) {
                cudaError_t q = cudaEventQuery(spec_done_evt);
                if (q == cudaSuccess) {
                    pthread_mutex_lock(&g_gui_spectrum_mtx);
                    if (!g_gui_spectrum_valid) {
                        for (int i = 0; i < GUI_SPECTRUM_BINS; ++i)
                            g_gui_spectrum_db[i] = spec_host[i];
                    } else {
                        const float alpha = 0.35f;
                        for (int i = 0; i < GUI_SPECTRUM_BINS; ++i)
                            g_gui_spectrum_db[i] = (1.0f - alpha) * g_gui_spectrum_db[i] + alpha * spec_host[i];
                    }
                    g_gui_spectrum_bins = GUI_SPECTRUM_BINS;
                    g_gui_spectrum_valid = 1;
                    pthread_mutex_unlock(&g_gui_spectrum_mtx);
                    spec_inflight = false;
                }
            }

            if (!spec_inflight && spec_sample_acc >= spec_period_samples && spec_fft >= 64) {
                spec_sample_acc = 0;
                int threadsSpec = 128;
                int blocksSpec = (GUI_SPECTRUM_BINS + threadsSpec - 1) / threadsSpec;
                gpu_spectrum_bins_kernel<<<blocksSpec, threadsSpec, 0, spec_stream>>>(
                    d_iq_out, spec_fft, GUI_SPECTRUM_BINS, d_spec_db);
                cudaMemcpyAsync(spec_host, d_spec_db,
                                (size_t)GUI_SPECTRUM_BINS * sizeof(float),
                                cudaMemcpyDeviceToHost, spec_stream);
                cudaEventRecord(spec_done_evt, spec_stream);
                spec_inflight = true;
            }

            /* Update GUI monitoring buffers */
            pthread_mutex_lock(&g_gui_spectrum_mtx);
            int td_samples = GUI_TIME_MON_SAMPLES;
            if (td_samples > samp_per_ms) td_samples = samp_per_ms;
            for (int i = 0; i < td_samples; ++i) {
                int src = (int)((long long)i * (long long)samp_per_ms / (long long)td_samples);
                if (src >= samp_per_ms) src = samp_per_ms - 1;
                g_gui_time_iq[2 * i] = host_iq[2 * src];       /* I */
                g_gui_time_iq[2 * i + 1] = host_iq[2 * src + 1]; /* Q */
            }
            for (int i = td_samples; i < GUI_TIME_MON_SAMPLES; ++i) {
                g_gui_time_iq[2 * i] = 0;
                g_gui_time_iq[2 * i + 1] = 0;
            }
            g_gui_time_samples = td_samples;
            g_gui_time_valid = (td_samples >= 8) ? 1 : 0;
            
            g_gui_spectrum_seq += 1;
            pthread_mutex_unlock(&g_gui_spectrum_mtx);

            usrp_send(host_iq, samp_per_ms);
            sample_count += (uint64_t)samp_per_ms;
            jam_ms_index += 1;
        }
    }

    cudaEventDestroy(spec_done_evt);
    cudaStreamDestroy(spec_stream);
    if (d_spec_db) cudaFree(d_spec_db);
    if (d_jam_ch) cudaFree(d_jam_ch);
    if (d_iq_out) cudaFree(d_iq_out);
    free(host_iq);
    g_sig_state.valid = false;
}

static void bdt_to_gpst_week_sow(int bdt_week, double bdt_sow,
                                 int *gps_week, double *gps_sow)
{
    const double week_sec = 604800.0;
    const double bdt0 = 1136073600.0;
    const double gps0 = 315964800.0;

    double unix_sec = bdt0 + (double)bdt_week * week_sec + bdt_sow - (double)utc_bdt_diff;
    double gpst_sec = unix_sec - gps0 + (double)utc_gpst_diff;

    int w = (int)floor(gpst_sec / week_sec);
    double sow = gpst_sec - (double)w * week_sec;
    if (sow < 0.0) {
        sow += week_sec;
        --w;
    }

    if (gps_week) *gps_week = w;
    if (gps_sow) *gps_sow = sow;
}

// NH 碼只有 20 bytes，繼續留在極速的 Constant Memory (__constant__)
__constant__ uint8_t d_nh20_bits[20];

// 為了讓 GPU 也能算 sin/cos，我們手刻一個極速版的近似運算 (比查表還快，不佔 VRAM)
__device__ inline void gpu_fast_sincos(double ph, float* co, float* si) {
    // 將相位收斂至 0 ~ 2*PI 之間
    ph -= floor(ph / CUDA_PI2) * CUDA_PI2;
    // 使用 CUDA 硬體加速的原生浮點三角函數指令
    sincosf((float)ph, si, co);
}

/*
 * Interference kernel: synthesize all-PRN signals using real PRN code and
 * pseudo-random NAV bits (no ephemeris dependency).
 */
__global__ void gpu_generate_jam_prn_kernel(
    const jam_prn_state_t *d_jam_ch,
    int n_jam_ch,
    int samp_per_ms,
    double fs,
    int16_t *d_iq_out)
{
    int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= samp_per_ms) return;

    float sum_I = 0.0f;
    float sum_Q = 0.0f;
    const float t_local = (float)k / (float)fs;

    for (int c = 0; c < n_jam_ch; ++c) {
        jam_prn_state_t ch = d_jam_ch[c];

        float carr_hz = ch.if_hz + ch.doppler_hz;
        float phase = ch.carr_phase0 + (float)CUDA_PI2 * carr_hz * t_local;

        float si, co;
        sincosf(phase, &si, &co);

        float code_phase = ch.code_phase0 + ch.chip_rate * t_local;
        int chip = ((int)code_phase) % CUDA_CODE_LEN;
        if (chip < 0) chip += CUDA_CODE_LEN;

        int16_t ca = ch.is_gps ? d_gps_ca_wave[ch.prn][chip] : d_ca_wave[ch.prn][chip];

        float s = ch.amp * (float)ca * (float)ch.nav_sign;
        sum_I += s * co;
        sum_Q += s * si;
    }

    float outI = fminf(fmaxf(sum_I, -32760.0f), 32760.0f);
    float outQ = fminf(fmaxf(sum_Q, -32760.0f), 32760.0f);
    d_iq_out[2 * k] = (int16_t)outI;
    d_iq_out[2 * k + 1] = (int16_t)outQ;
}

// 這是要在 GPU 上平行執行的核心運算
__global__ void gpu_generate_1ms_kernel(
    const channel_t* d_channels,
    int n_ch,
    int samp_per_ms,
    double fs,
    int16_t* d_iq_out
) {
    int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= samp_per_ms) return;

    float sum_I = 0.0f;
    float sum_Q = 0.0f;

    for (int c = 0; c < n_ch; ++c) {
        channel_t ch = d_channels[c];

        double dt_k = (double)k / fs; 
        
        // 嚴格雙精度計算當下相位
        double current_phase = ch.carr_phase + CUDA_PI2 * (ch.fd + 0.5 * ch.fd_dot * dt_k) * dt_k;
        double current_code_phase = ch.code_phase + (ch.code_rate + 0.5 * ch.code_rate_dot * dt_k) * dt_k;
        double current_amp = ch.amp + ch.amp_dot * dt_k;

        int chip = ((int)current_code_phase) % CUDA_CODE_LEN;
        if (chip < 0) chip += CUDA_CODE_LEN;

        int16_t ca = ch.is_gps ? d_gps_ca_wave[ch.prn][chip] : d_ca_wave[ch.prn][chip];
        
        int total_ms_passed = (int)(current_code_phase / CUDA_CODE_LEN);
        int current_ms_count = (ch.ms_count + total_ms_passed) % 20;
        int bits_passed = (ch.ms_count + total_ms_passed) / 20;
        int current_bit_ptr = (ch.bit_ptr + bits_passed) % 300;

        uint8_t nh = ch.is_gps ? 0 : d_nh20_bits[current_ms_count];
        int16_t nb = (ch.nav_bits[current_bit_ptr] ^ nh) ? -1 : +1;

        // 【修正】：使用原生的雙精度 sincos，徹底消滅相位雜訊！
        double co, si;
        sincos(current_phase, &si, &co);
        
        double s = current_amp * ca * nb;
        sum_I += (float)(s * co);
        sum_Q += (float)(s * si);
    }

    float sat_I = fminf(fmaxf(sum_I, -32760.0f), 32760.0f);
    float sat_Q = fminf(fmaxf(sum_Q, -32760.0f), 32760.0f);
    d_iq_out[2 * k] = (int16_t)sat_I;
    d_iq_out[2 * k + 1] = (int16_t)sat_Q;
}

__global__ void gpu_spectrum_bins_kernel(
    const int16_t* d_iq_out,
    int n_fft,
    int bins,
    float* d_out_db
) {
    int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= bins) return;

    int k = (b * n_fft) / bins - (n_fft / 2);
    double re = 0.0;
    double im = 0.0;

    for (int n = 0; n < n_fft; ++n) {
        double w = 0.5 - 0.5 * cos(CUDA_PI2 * (double)n / (double)(n_fft - 1));
        double xr = (double)d_iq_out[2 * n] * w;
        double xi = (double)d_iq_out[2 * n + 1] * w;

        double ang = CUDA_PI2 * (double)k * (double)n / (double)n_fft;
        double s, c;
        sincos(ang, &s, &c);

        re += xr * c + xi * s;
        im += xi * c - xr * s;
    }

    double p = (re * re + im * im) / ((double)n_fft * (double)n_fft) + 1e-9;
    d_out_db[b] = (float)(10.0 * log10(p));
}

/* --------------------------- 起始時間與星曆 toe 檢查 ------------------------------*/
static void check_ephemeris_age(int week,double sow)
{
    double t = week*604800.0 + sow;
    int warn = 0;
    for(int prn=1; prn<=prn_max; ++prn){
        if(eph[prn].prn==0) continue;
        double toe = eph[prn].week*604800.0 + eph[prn].toe;
        double diff = fabs(t - toe);
        if(diff > 2*86400.0){
            fprintf(stderr,
                    "[warn] PRN%02d toe %.1f days away from start\n",
                    prn, diff/86400.0);
            warn = 1;
        }
    }
    if(warn)
        fputs("建議使用接近星曆 toe 的起始時間以避免 tk 錯誤\n", stderr);
}

/* --------------------------- Compute corrected pseudorange and range rate ------------------------------*/
static void compute_range_rate(int prn,int week,double sow,
                               const coord_t *u,const double uvel[3],
                               double sat_rx[3], double vsat_rx[3],
                               double *rho,double *rdot,double los[3])
{
    /* Receiver time in seconds since BDT epoch */
    double t_rx = week*604800.0 + sow;

    /* Satellite clock at receiver time for initial guess */
    double clk_rx[2], pos_dummy[3], vel_dummy[3];
    calc_sat_position_velocity(prn, week, sow, pos_dummy, vel_dummy, clk_rx);

    /* Initial transmit time estimate */
    double t_tx = t_rx - clk_rx[0];

    /* Iterate once with geometric range */
    int week_sv = (int)(t_tx/604800.0);
    double sow_sv = t_tx - week_sv*604800.0;
    double r_tx[3], v_tx[3], clk_sv[2];
    calc_sat_position_velocity(prn, week_sv, sow_sv, r_tx, v_tx, clk_sv);
    double dx = r_tx[0] - u->xyz[0];
    double dy = r_tx[1] - u->xyz[1];
    double dz = r_tx[2] - u->xyz[2];
    double range = hypot(hypot(dx,dy),dz);
    double tau = range / CLIGHT;

    /* Refined transmit time */
    t_tx = t_rx - tau - clk_sv[0];
    week_sv = (int)(t_tx/604800.0);
    sow_sv  = t_tx - week_sv*604800.0;
    calc_sat_position_velocity(prn, week_sv, sow_sv, r_tx, v_tx, clk_sv);

    /* Time difference between tx and rx */
    tau = t_rx - t_tx;

    /* Rotate satellite state to receiver time */
    double A = OMEGA_E * tau;
    double cA = cos(A), sA = sin(A);
    double x_rx =  cA*r_tx[0] + sA*r_tx[1];
    double y_rx = -sA*r_tx[0] + cA*r_tx[1];
    double z_rx =  r_tx[2];

    double vx_rot =  cA*v_tx[0] + sA*v_tx[1];
    double vy_rot = -sA*v_tx[0] + cA*v_tx[1];
    double vz_rot =  v_tx[2];

    double vx_rx = vx_rot + (sA*OMEGA_E)*r_tx[0] + (-cA*OMEGA_E)*r_tx[1];
    double vy_rx = vy_rot + (cA*OMEGA_E)*r_tx[0] + ( sA*OMEGA_E)*r_tx[1];
    double vz_rx = vz_rot;

    if(sat_rx){ sat_rx[0]=x_rx; sat_rx[1]=y_rx; sat_rx[2]=z_rx; }
    if(vsat_rx){ vsat_rx[0]=vx_rx; vsat_rx[1]=vy_rx; vsat_rx[2]=vz_rx; }

    dx = x_rx - u->xyz[0];
    dy = y_rx - u->xyz[1];
    dz = z_rx - u->xyz[2];
    range = hypot(hypot(dx,dy),dz);

    double ex = dx / range;
    double ey = dy / range;
    double ez = dz / range;
    if(los){ los[0]=ex; los[1]=ey; los[2]=ez; }

    if (rho) *rho = range - CLIGHT * clk_sv[0];

    if (rdot) {
        double vrx = uvel ? uvel[0] : 0.0;
        double vry = uvel ? uvel[1] : 0.0;
        double vrz = uvel ? uvel[2] : 0.0;
        double dvx = vx_rx - vrx;
        double dvy = vy_rx - vry;
        double dvz = vz_rx - vrz;
        *rdot = ex*dvx + ey*dvy + ez*dvz;  /* m/s */
    }
}

static void compute_range_rate_gps(int prn,int week,double sow,
                                   const coord_t *u,const double uvel[3],
                                   double sat_rx[3], double vsat_rx[3],
                                   double *rho,double *rdot,double los[3])
{
    double t_rx = week*604800.0 + sow;

    double clk_rx[2], pos_dummy[3], vel_dummy[3];
    calc_gps_position_velocity(prn, week, sow, pos_dummy, vel_dummy, clk_rx);

    double t_tx = t_rx - clk_rx[0];

    int week_sv = (int)(t_tx/604800.0);
    double sow_sv = t_tx - week_sv*604800.0;
    double r_tx[3], v_tx[3], clk_sv[2];
    calc_gps_position_velocity(prn, week_sv, sow_sv, r_tx, v_tx, clk_sv);
    double dx = r_tx[0] - u->xyz[0];
    double dy = r_tx[1] - u->xyz[1];
    double dz = r_tx[2] - u->xyz[2];
    double range = hypot(hypot(dx,dy),dz);
    double tau = range / CLIGHT;

    t_tx = t_rx - tau - clk_sv[0];
    week_sv = (int)(t_tx/604800.0);
    sow_sv  = t_tx - week_sv*604800.0;
    calc_gps_position_velocity(prn, week_sv, sow_sv, r_tx, v_tx, clk_sv);

    tau = t_rx - t_tx;

    double A = OMEGA_E * tau;
    double cA = cos(A), sA = sin(A);
    double x_rx =  cA*r_tx[0] + sA*r_tx[1];
    double y_rx = -sA*r_tx[0] + cA*r_tx[1];
    double z_rx =  r_tx[2];

    double vx_rot =  cA*v_tx[0] + sA*v_tx[1];
    double vy_rot = -sA*v_tx[0] + cA*v_tx[1];
    double vz_rot =  v_tx[2];

    double vx_rx = vx_rot + (sA*OMEGA_E)*r_tx[0] + (-cA*OMEGA_E)*r_tx[1];
    double vy_rx = vy_rot + (cA*OMEGA_E)*r_tx[0] + ( sA*OMEGA_E)*r_tx[1];
    double vz_rx = vz_rot;

    if(sat_rx){ sat_rx[0]=x_rx; sat_rx[1]=y_rx; sat_rx[2]=z_rx; }
    if(vsat_rx){ vsat_rx[0]=vx_rx; vsat_rx[1]=vy_rx; vsat_rx[2]=vz_rx; }

    dx = x_rx - u->xyz[0];
    dy = y_rx - u->xyz[1];
    dz = z_rx - u->xyz[2];
    range = hypot(hypot(dx,dy),dz);

    double ex = dx / range;
    double ey = dy / range;
    double ez = dz / range;
    if(los){ los[0]=ex; los[1]=ey; los[2]=ez; }

    if (rho) *rho = range - CLIGHT * clk_sv[0];

    if (rdot) {
        double vrx = uvel ? uvel[0] : 0.0;
        double vry = uvel ? uvel[1] : 0.0;
        double vrz = uvel ? uvel[2] : 0.0;
        double dvx = vx_rx - vrx;
        double dvy = vy_rx - vry;
        double dvz = vz_rx - vrz;
        *rdot = ex*dvx + ey*dvy + ez*dvz;
    }
}

/* --------------------------- Channel selection ------------------------------*/
int select_channels(channel_t *ch,int *n,const coord_t*u,
                    int single_prn,bool meo_only,bool prn37_only,
                    int signal_mode,int max_ch)
{
    if (signal_mode == SIG_MODE_MIXED) {
        struct cand { int prn; double elev, rho; } c_gps[CUDA_GPS_PRN_MAX];
        struct cand c_bds[63];
        int m_gps = 0;
        int m_bds = 0;

        int gps_week = 0;
        double gps_sow = 0.0;
        bdt_to_gpst_week_sow(u->week, u->sow, &gps_week, &gps_sow);

        for (int prn = 1; prn <= CUDA_GPS_PRN_MAX; ++prn) {
            if (gps_eph[prn].prn == 0) continue;

            double sat[3], rho, rdot;
            compute_range_rate_gps(prn, gps_week, gps_sow, u, NULL, sat, NULL, &rho, &rdot, NULL);
            double enu[3];
            ecef_to_enu(u, sat, enu);
            double el = enu_elevation_deg(enu);
            if (el < 5.0) continue;

            if (g_enable_iono) {
                double az_deg = atan2(enu[0], enu[1]) * 180.0 / M_PI;
                double lat_deg = u->llh[0] * 180.0 / M_PI;
                double lon_deg = u->llh[1] * 180.0 / M_PI;
                rho += iono_delay(lat_deg, lon_deg, az_deg, el,
                                  u->sow, iono_alpha, iono_beta,
                                  F_GPS_L1, F_B1I, NULL);
            }

            c_gps[m_gps++] = (struct cand){prn, el, rho};
        }

        for (int i = 0; i < m_gps - 1; ++i) for (int j = i + 1; j < m_gps; ++j) {
            if ((c_gps[j].rho < c_gps[i].rho) ||
                (fabs(c_gps[j].rho - c_gps[i].rho) < 1.0 && c_gps[j].elev > c_gps[i].elev)) {
                struct cand t = c_gps[i]; c_gps[i] = c_gps[j]; c_gps[j] = t;
            }
        }

        for (int prn = 1; prn <= prn_max; ++prn) {
            if (prn37_only && prn > 37) continue;
            if (is_geo_prn(prn)) continue;
            if (meo_only && !is_meo_prn(prn)) continue;
            if (eph[prn].prn == 0) continue;

            double sat[3], rho, rdot;
            compute_range_rate(prn, u->week, u->sow, u, NULL, sat, NULL, &rho, &rdot, NULL);
            double enu[3]; ecef_to_enu(u, sat, enu);
            double el = enu_elevation_deg(enu);
            if (el < 5.0) continue;
            double az_deg = atan2(enu[0], enu[1]) * 180.0 / M_PI;
            double lat_deg = u->llh[0] * 180.0 / M_PI;
            double lon_deg = u->llh[1] * 180.0 / M_PI;
            if (g_enable_iono)
                rho += iono_delay(lat_deg, lon_deg, az_deg, el,
                                  u->sow, iono_alpha, iono_beta,
                                  F_B1I, F_B1I, NULL);
            c_bds[m_bds++] = (struct cand){prn, el, rho};
        }

        for (int i = 0; i < m_bds - 1; ++i) for (int j = i + 1; j < m_bds; ++j) {
            if (c_bds[j].elev > c_bds[i].elev) {
                struct cand t = c_bds[i]; c_bds[i] = c_bds[j]; c_bds[j] = t;
            }
        }

        int n_total = max_ch;
        int n_bds_target = n_total / 2;
        int n_gps_target = n_total - n_bds_target;
        int n_bds = (m_bds < n_bds_target) ? m_bds : n_bds_target;
        int n_gps = (m_gps < n_gps_target) ? m_gps : n_gps_target;

        int need = n_total - (n_bds + n_gps);
        int rem_bds = m_bds - n_bds;
        int rem_gps = m_gps - n_gps;
        while (need > 0 && (rem_bds > 0 || rem_gps > 0)) {
            if (rem_bds >= rem_gps && rem_bds > 0) {
                ++n_bds;
                --rem_bds;
            } else if (rem_gps > 0) {
                ++n_gps;
                --rem_gps;
            }
            --need;
        }

        int idx = 0;
        for (int i = 0; i < n_bds && idx < max_ch; ++i)
            channel_reset(&ch[idx++], c_bds[i].prn, c_bds[i].rho, 0);
        for (int i = 0; i < n_gps && idx < max_ch; ++i)
            channel_reset(&ch[idx++], c_gps[i].prn, c_gps[i].rho, 1);
        *n = idx;
        return *n;
    }

    if (signal_mode == SIG_MODE_GPS || single_prn < 0) {
        int forced_single_prn = 0;
        if (single_prn < 0) {
            forced_single_prn = -single_prn; // Backward-compatible: negative PRN means GPS single-sat mode.
        } else if (single_prn > 0) {
            forced_single_prn = single_prn;
        }

        struct cand { int prn; double elev, rho; } c[CUDA_GPS_PRN_MAX];
        int m = 0;

        int gps_week = 0;
        double gps_sow = 0.0;
        bdt_to_gpst_week_sow(u->week, u->sow, &gps_week, &gps_sow);

        for (int prn = 1; prn <= CUDA_GPS_PRN_MAX; ++prn) {
            if (forced_single_prn > 0 && prn != forced_single_prn) continue;
            if (gps_eph[prn].prn == 0) continue;

            double sat[3], rho, rdot;
            compute_range_rate_gps(prn, gps_week, gps_sow, u, NULL, sat, NULL, &rho, &rdot, NULL);
            double enu[3];
            ecef_to_enu(u, sat, enu);
            double el = enu_elevation_deg(enu);
            if (el < 5.0) continue;

            if (g_enable_iono) {
                double az_deg = atan2(enu[0], enu[1]) * 180.0 / M_PI;
                double lat_deg = u->llh[0] * 180.0 / M_PI;
                double lon_deg = u->llh[1] * 180.0 / M_PI;
                rho += iono_delay(lat_deg, lon_deg, az_deg, el,
                                  u->sow, iono_alpha, iono_beta,
                                  F_GPS_L1, F_B1I, NULL);
            }

            c[m++] = (struct cand){prn, el, rho};
        }

        for (int i = 0; i < m - 1; ++i) for (int j = i + 1; j < m; ++j) {
            /* Prefer satellites with smaller geometric range; use elevation as tie-breaker. */
            if ((c[j].rho < c[i].rho) ||
                (fabs(c[j].rho - c[i].rho) < 1.0 && c[j].elev > c[i].elev)) {
                struct cand t = c[i]; c[i] = c[j]; c[j] = t;
            }
        }

        *n = m < MAX_CH ? m : MAX_CH;
        for (int i = 0; i < *n; ++i)
            channel_reset(&ch[i], c[i].prn, c[i].rho, 1);
        return *n;
    }

    struct cand{int prn;double elev,rho,rdot;} c[63];
    int m=0;
    for(int prn=1;prn<=prn_max;++prn){
        if(single_prn>0 && prn!=single_prn) continue;
        if(prn37_only && prn>37) continue;
        if(is_geo_prn(prn)) continue;
        if(meo_only && !is_meo_prn(prn)) continue;
        double sat[3], rho, rdot;
        compute_range_rate(prn,u->week,u->sow,u,NULL,sat,NULL,&rho,&rdot,NULL);
        double enu[3]; ecef_to_enu(u,sat,enu);
        double el=enu_elevation_deg(enu); if(el<5.0)continue;
        double az_deg = atan2(enu[0], enu[1]) * 180.0 / M_PI;
        double lat_deg = u->llh[0] * 180.0 / M_PI;
        double lon_deg = u->llh[1] * 180.0 / M_PI;
        if(g_enable_iono)
            rho += iono_delay(lat_deg, lon_deg, az_deg, el,
                               u->sow, iono_alpha, iono_beta,
                               F_B1I, F_B1I, NULL);
        c[m++] = (struct cand){prn,el,rho,rdot};
    }
    /* sort by elevation (desc) */
    for(int i=0;i<m-1;++i) for(int j=i+1;j<m;++j){
        if(c[j].elev>c[i].elev){
            struct cand t=c[i];c[i]=c[j];c[j]=t;
        }
    }
    *n = m<MAX_CH?m:MAX_CH;
    for(int i=0;i<*n;++i)
        channel_reset(&ch[i],c[i].prn,c[i].rho,0);
    return *n;
}

/* --------------------------- 依照使用者軌跡更新通道，使用目前與下一步的幾何資訊 ------------------------------*/
static void update_channels_path(channel_t *ch,int n,
                                 const coord_t *u,const double uvel[3],
                                 const coord_t *u_next,const double uvel_next[3],
                                 double gain,double target_cn0,int step_ms)
{
    double dt = step_ms * 0.001; /* seconds */

    double t_abs = u->week*604800.0 + u->sow;
    /* 第一輪：計算幾何與可視衛星數 */
    double rho[MAX_CH], rdot[MAX_CH], el[MAX_CH];
    double rho2[MAX_CH], rdot2[MAX_CH], el2[MAX_CH];
    double fd2[MAX_CH], cr2[MAX_CH];
    double sat[MAX_CH][3], vsat[MAX_CH][3], los[MAX_CH][3];
    int n_vis_now = 0, n_vis_next = 0;

    double t_next = t_abs + dt;
    int week2 = (int)(t_next/604800.0);
    double sow2 = t_next - week2*604800.0;

    int gps_week_now = 0;
    double gps_sow_now = 0.0;
    int gps_week_next = 0;
    double gps_sow_next = 0.0;
    bdt_to_gpst_week_sow(u->week, u->sow, &gps_week_now, &gps_sow_now);
    bdt_to_gpst_week_sow(week2, sow2, &gps_week_next, &gps_sow_next);

    for(int i=0;i<n;++i){
        int prn = ch[i].prn;
        if (ch[i].is_gps)
            compute_range_rate_gps(prn,gps_week_now,gps_sow_now,u,uvel,
                                   sat[i],vsat[i],&rho[i],&rdot[i],los[i]);
        else
            compute_range_rate(prn,u->week,u->sow,u,uvel,
                               sat[i],vsat[i],&rho[i],&rdot[i],los[i]);
        double enu[3]; ecef_to_enu(u,sat[i],enu);
        el[i] = enu_elevation_deg(enu);
        if(el[i] >= 5.0) n_vis_now++;
        double az_deg = atan2(enu[0], enu[1]) * 180.0 / M_PI;
        double lat_deg = u->llh[0] * 180.0 / M_PI;
        double lon_deg = u->llh[1] * 180.0 / M_PI;
        if(g_enable_iono) {
            double f_sig = ch[i].is_gps ? F_GPS_L1 : F_B1I;
            rho[i] += iono_delay(lat_deg, lon_deg, az_deg, el[i],
                                 u->sow, iono_alpha, iono_beta,
                                 f_sig, F_B1I, NULL);
        }

        double sat2[3];
        if (ch[i].is_gps)
            compute_range_rate_gps(prn,gps_week_next,gps_sow_next,u_next,uvel_next,
                                   sat2,NULL,&rho2[i],&rdot2[i],NULL);
        else
            compute_range_rate(prn,week2,sow2,u_next,uvel_next,
                               sat2,NULL,&rho2[i],&rdot2[i],NULL);
        double enu2[3]; ecef_to_enu(u_next,sat2,enu2);
        el2[i] = enu_elevation_deg(enu2);
        if(el2[i] >= 5.0) n_vis_next++;
        double az2_deg = atan2(enu2[0], enu2[1]) * 180.0 / M_PI;
        double lat2_deg = u_next->llh[0] * 180.0 / M_PI;
        double lon2_deg = u_next->llh[1] * 180.0 / M_PI;
        if(g_enable_iono) {
            double f_sig = ch[i].is_gps ? F_GPS_L1 : F_B1I;
            rho2[i] += iono_delay(lat2_deg, lon2_deg, az2_deg, el2[i],
                                  sow2, iono_alpha, iono_beta,
                                  f_sig, F_B1I, NULL);
        }
        double f_carrier = ch[i].is_gps ? F_GPS_L1 : F_B1I;
        double if_offset = ch[i].is_gps ? g_gps_if_offset_hz : g_bds_if_offset_hz;
        fd2[i] = if_offset - f_carrier*rdot2[i]/CLIGHT;
        cr2[i] = CHIPRATE*(1.0 - rdot2[i]/CLIGHT);
    }

    if(n_vis_now < 1) n_vis_now = 1;           /* avoid div-by-zero */
    if(n_vis_next < 1) n_vis_next = 1;

    /* 第二輪：更新通道狀態 */
    for(int i=0;i<n;++i){
        // channel_set_time(&ch[i], rho[i]);
        update_channel_dynamics(&ch[i],rho[i],rdot[i],el[i],
                                gain,target_cn0,n_vis_now,step_ms);
        if(el[i] < 5.0) ch[i].amp = 0.0;     /* below horizon → mute */

        double A2 = predict_next_amp(&ch[i], rho2[i], el2[i],
                                     gain,target_cn0,n_vis_next,step_ms);
        if(el2[i] < 5.0) A2 = 0.0;
        ch[i].fd_dot = (fd2[i] - ch[i].fd) / dt;
        ch[i].code_rate_dot = (cr2[i] - ch[i].code_rate) / dt;
        ch[i].amp_dot = (A2 - ch[i].amp) / dt;
    }
}
/* --------------------------- 信號產生 ------------------------------*/
void generate_signal(const sim_config_t *cfg)
{
    ensure_prn_tables_ready();

    coord_t usr={0};
    path_t path={0};
    if(cfg->path_type==1)       load_path_xyz(cfg->path_file,&path);
    else if(cfg->path_type==2)  load_path_llh(cfg->path_file,&path);
    else if(cfg->path_type==3)  load_path_nmea(cfg->path_file,&path);
    if(cfg->path_type!=0 && path.n==0){
        fputs("path read error\n",stderr);
        free_path(&path);
        return;
    }
    if (cfg->path_type != 0) {
        coord_t prev_anchor = {0};
        double llh_rad[3] = {
            cfg->llh[0] * M_PI / 180.0,
            cfg->llh[1] * M_PI / 180.0,
            cfg->llh[2]
        };
        lla_to_ecef(llh_rad, &prev_anchor);

        coord_t end_anchor = {0};
        interpolate_path(&path, (double)(path.n - 1) / PATH_UPDATE_HZ, &end_anchor);

        path_clear_anchors(&path);
        path_set_prev_anchor_xyz(&path, prev_anchor.xyz);
        path_set_next_anchor_xyz(&path, end_anchor.xyz);
    }
    if(cfg->path_type==0){
        double llh_rad[3]={cfg->llh[0]*M_PI/180.0,
                           cfg->llh[1]*M_PI/180.0,
                           cfg->llh[2]};
        lla_to_ecef(llh_rad,&usr);
    } else { interpolate_path(&path,0.0,&usr); ecef_to_lla(usr.xyz,&usr); }
    if(utc_to_bdt(cfg->time_start,&usr.week,&usr.sow)!=0){
        fputs("UTC format err\n",stderr);
        free_path(&path);
        return;
    }

    coord_t ref_llh=usr;              /* 保存經緯度作旋轉基準 */
    static_user_at(usr.week,usr.sow,&ref_llh,&usr,NULL);

    /* 檢查 start 時間是否與星曆 toe 接近 */
    check_ephemeris_age(usr.week, usr.sow);

    g_t_tx = usr.week*604800.0 + usr.sow;

    double start_bdt = usr.week*604800.0 + usr.sow;
    if (cfg->interference_mode) {
        generate_interference_signal(cfg, &path, &usr, &ref_llh, start_bdt);
        free_path(&path);
        return;
    }

    bool can_reuse = false;
    if (g_sig_state.valid &&
        fabs(start_bdt - g_sig_state.next_bdt) < 1e-6 &&
        fabs(cfg->fs - g_sig_state.fs) < 1.0 &&
        cfg->single_prn == g_sig_state.single_prn &&
        cfg->meo_only == g_sig_state.meo_only &&
        cfg->prn37_only == g_sig_state.prn37_only &&
        cfg->signal_mode == g_sig_state.signal_mode) {
        can_reuse = true;
    }

    channel_t ch[MAX_CH];
    int n_ch = 0;
    if (can_reuse) {
        n_ch = g_sig_state.n_ch;
        if (n_ch > MAX_CH) n_ch = MAX_CH;
        memcpy(ch, g_sig_state.ch, (size_t)n_ch * sizeof(channel_t));
    } else {
        /* 只有在全新段落才重建通道，避免相位重置 */
        srand(cfg->seed);
        select_channels(ch,&n_ch,&usr,cfg->single_prn,
                        cfg->meo_only,cfg->prn37_only,cfg->signal_mode,cfg->max_ch);
    }

    /* 限制通道數量到使用者設定的max_ch */
    if (n_ch > cfg->max_ch) {
        n_ch = cfg->max_ch;
    }

    double fs = cfg->fs;
    int samp_per_ms = (int)(fs/1000.0 + 0.5);
    channel_set_fs(fs);                   /* ensure dynamics use correct Fs */

    // 注意：USRP 在 `main.c` 已初始化並排程，此處不再重複初始化。

    // (移除了龐大且用不到的 tmpI 與 tmpQ，節省 CPU 記憶體)
    // =========================================================
    // 👇 新增：配置 GPU 專屬記憶體並上傳北斗測距碼 👇
    // =========================================================
    channel_t* d_channels;
    int16_t *d_iq_out;
    int16_t *host_iq;
    float *d_spec_db;
    size_t iq_bytes = (size_t)(2 * samp_per_ms) * sizeof(int16_t);
    cudaMalloc((void**)&d_channels, MAX_CH * sizeof(channel_t));
    cudaMalloc((void**)&d_iq_out, iq_bytes);
    cudaMallocHost((void**)&host_iq, iq_bytes);
    cudaMalloc((void**)&d_spec_db, GUI_SPECTRUM_BINS * sizeof(float));

    int spec_fft = GUI_SPEC_FFT_POINTS;
    if (spec_fft > samp_per_ms) spec_fft = samp_per_ms;
    if (spec_fft < 64) spec_fft = 64;
    int spec_bins = GUI_SPECTRUM_BINS;
    float spec_host[GUI_SPECTRUM_BINS] = {0.0f};
    uint64_t spec_sample_acc = 0;
    uint64_t spec_period_samples = (uint64_t)(fs / (double)GUI_SPEC_FPS + 0.5);
    if (spec_period_samples < (uint64_t)samp_per_ms) spec_period_samples = (uint64_t)samp_per_ms;

    cudaStream_t spec_stream;
    cudaEvent_t spec_done_evt;
    cudaStreamCreateWithFlags(&spec_stream, cudaStreamNonBlocking);
    cudaEventCreateWithFlags(&spec_done_evt, cudaEventDisableTiming);
    bool spec_inflight = false;

    // 產生測距碼並送到 GPU 的 VRAM
    int16_t local_ca[64][CUDA_CODE_LEN];
    int16_t local_gps_ca[33][CUDA_CODE_LEN];
    for(int p=1; p<=63; ++p) {
        for(int i=0; i<CUDA_CODE_LEN; ++i) {
            local_ca[p][i] = prn_code[p][i] ? 1 : -1;
        }
    }
    for(int p=1; p<=CUDA_GPS_PRN_MAX; ++p) {
        for(int i=0; i<CUDA_CODE_LEN; ++i) {
            int src = i >> 1;
            local_gps_ca[p][i] = gps_prn_code[p][src] ? 1 : -1;
        }
    }
    cudaMemcpyToSymbol(d_ca_wave, local_ca, sizeof(local_ca));
    cudaMemcpyToSymbol(d_gps_ca_wave, local_gps_ca, sizeof(local_gps_ca));
    cudaMemcpyToSymbol(d_nh20_bits, nh20_bits, sizeof(nh20_bits));
    // =========================================================

    const int STEP_MS = cfg->step_ms;
    const uint64_t total_ms=(uint64_t)cfg->duration*1000;
    uint64_t sample_count = 0;
    for(uint64_t ms=0; ms<total_ms; ms+=STEP_MS)
    {
        if (g_runtime_abort) break;

        g_t_tx = start_bdt + sample_count/fs;
        int week = (int)(g_t_tx/604800.0);
        double sow = g_t_tx - week*604800.0;

        double dt = STEP_MS * 0.001;
        double uvel[3], uvel_next[3];
        coord_t usr_next={0};
        if(cfg->path_type==0){
            /* Static user: include Earth-rotation velocity */
            static_user_at(week, sow, &ref_llh, &usr, uvel);
            static_user_at(week, sow + dt, &ref_llh, &usr_next, uvel_next);
            update_channels_path(ch,n_ch,&usr,uvel,&usr_next,uvel_next,
                                 cfg->gain,g_target_cn0,STEP_MS);
        } else if(cfg->path_type==1 || cfg->path_type==2){
            const int is_llh = (cfg->path_type == 2) ? 1 : 0;
            const double t1 = (double)ms / 1000.0;
            const double t2 = (double)(ms + STEP_MS) / 1000.0;
            double uvel_raw[3], uvel_next_raw[3];

            interpolate_path_kinematic(&path, t1, &usr, uvel_raw, is_llh);
            interpolate_path_kinematic(&path, t2, &usr_next, uvel_next_raw, is_llh);

            if (!is_llh) {
                ecef_to_lla(usr.xyz, &usr);
                ecef_to_lla(usr_next.xyz, &usr_next);
            }

            usr.week = week;
            usr.sow = sow;
            usr_next.week = week;
            usr_next.sow = sow + dt;

            uvel[0] = uvel_raw[0] - OMEGA_E * usr.xyz[1];
            uvel[1] = uvel_raw[1] + OMEGA_E * usr.xyz[0];
            uvel[2] = uvel_raw[2];

            uvel_next[0] = uvel_next_raw[0] - OMEGA_E * usr_next.xyz[1];
            uvel_next[1] = uvel_next_raw[1] + OMEGA_E * usr_next.xyz[0];
            uvel_next[2] = uvel_next_raw[2];

            update_channels_path(ch,n_ch,&usr,uvel,&usr_next,uvel_next,
                                 cfg->gain,g_target_cn0,STEP_MS);
        } else {
            double llh0[3], llh1[3], llh2[3];
            interpolate_path_llh(&path, ms/1000.0, llh0);
            interpolate_path_llh(&path, (ms+STEP_MS)/1000.0, llh1);
            interpolate_path_llh(&path, (ms+2*STEP_MS)/1000.0, llh2);
            coord_t ref0={0}, ref1={0}, ref2={0};
            ref0.llh[0]=llh0[0]; ref0.llh[1]=llh0[1]; ref0.llh[2]=llh0[2];
            ref1.llh[0]=llh1[0]; ref1.llh[1]=llh1[1]; ref1.llh[2]=llh1[2];
            ref2.llh[0]=llh2[0]; ref2.llh[1]=llh2[1]; ref2.llh[2]=llh2[2];
            static_user_at(week,sow,&ref0,&usr,NULL);
            static_user_at(week,sow+dt,&ref1,&usr_next,NULL);
            coord_t usr2; static_user_at(week,sow+2*dt,&ref2,&usr2,NULL);
            uvel[0] = (usr_next.xyz[0] - usr.xyz[0])/dt - OMEGA_E * usr.xyz[1];
            uvel[1] = (usr_next.xyz[1] - usr.xyz[1])/dt + OMEGA_E * usr.xyz[0];
            uvel[2] = (usr_next.xyz[2] - usr.xyz[2])/dt;
            
            uvel_next[0] = (usr2.xyz[0] - usr_next.xyz[0])/dt - OMEGA_E * usr_next.xyz[1];
            uvel_next[1] = (usr2.xyz[1] - usr_next.xyz[1])/dt + OMEGA_E * usr_next.xyz[0];
            uvel_next[2] = (usr2.xyz[2] - usr_next.xyz[2])/dt;
            update_channels_path(ch,n_ch,&usr,uvel,&usr_next,uvel_next,
                                 cfg->gain,g_target_cn0,STEP_MS);
        }

        g_receiver_lat_deg = usr.llh[0] * 180.0 / M_PI;
        g_receiver_lon_deg = usr.llh[1] * 180.0 / M_PI;
        g_receiver_valid = 1;

        if(ms==0 && cfg->print_ch_info){
            for(int i=0;i<n_ch;++i){
                double f_carrier = ch[i].is_gps ? F_GPS_L1 : F_B1I;
                double if_offset = ch[i].is_gps ? g_gps_if_offset_hz : g_bds_if_offset_hz;
                double rdot = -(ch[i].fd - if_offset) * CLIGHT / f_carrier;
                printf("[ch%02d] rdot %.2f fd %.2fHz\n", ch[i].prn, rdot, ch[i].fd);
            }
        }

        /* STEP_MS 次 1ms 取樣 */
        for(int step=0;step<STEP_MS;++step){
            if (g_runtime_abort) break;

            g_t_tx = start_bdt + sample_count/fs;
            // (注意：這裡不需要 memset accI 和 accQ 了，因為 GPU 會直接寫入覆蓋數值)

            // =========================================================
            // 👇 啟動 GPU 降維打擊：取代原本的 CPU 苦力活 👇
            // =========================================================
            
            // 1. 將當前可見衛星的「最新狀態」搭高鐵送到 GPU
            cudaMemcpy(d_channels, ch, n_ch * sizeof(channel_t), cudaMemcpyHostToDevice);

            // 2. 呼叫 GPU 核心 (瞬間召喚 5200 個執行緒平行運算)
            int threadsPerBlock = 256;
            int blocksPerGrid = (samp_per_ms + threadsPerBlock - 1) / threadsPerBlock;
            gpu_generate_1ms_kernel<<<blocksPerGrid, threadsPerBlock>>>(
                d_channels, n_ch, samp_per_ms, fs, d_iq_out
            );

            // 3. 單次拷貝 GPU 打包後的交錯 I/Q 到 Pinned Memory
            cudaMemcpy(host_iq, d_iq_out, iq_bytes, cudaMemcpyDeviceToHost);

            spec_sample_acc += (uint64_t)samp_per_ms;

            if (spec_inflight) {
                cudaError_t q = cudaEventQuery(spec_done_evt);
                if (q == cudaSuccess) {
                    pthread_mutex_lock(&g_gui_spectrum_mtx);
                    if (!g_gui_spectrum_valid) {
                        for (int i = 0; i < spec_bins; ++i)
                            g_gui_spectrum_db[i] = spec_host[i];
                    } else {
                        const float alpha = 0.35f;
                        for (int i = 0; i < spec_bins; ++i)
                            g_gui_spectrum_db[i] = (1.0f - alpha) * g_gui_spectrum_db[i] + alpha * spec_host[i];
                    }
                    int td_samples = GUI_TIME_MON_SAMPLES;
                    if (td_samples > samp_per_ms) td_samples = samp_per_ms;
                    for (int i = 0; i < td_samples; ++i) {
                        int src = (int)((long long)i * (long long)samp_per_ms / (long long)td_samples);
                        if (src >= samp_per_ms) src = samp_per_ms - 1;
                        g_gui_time_iq[2 * i] = host_iq[2 * src];
                        g_gui_time_iq[2 * i + 1] = host_iq[2 * src + 1];
                    }
                    for (int i = td_samples; i < GUI_TIME_MON_SAMPLES; ++i) {
                        g_gui_time_iq[2 * i] = 0;
                        g_gui_time_iq[2 * i + 1] = 0;
                    }
                    g_gui_time_samples = td_samples;
                    g_gui_time_valid = (td_samples >= 8) ? 1 : 0;
                    g_gui_spectrum_bins = spec_bins;
                    g_gui_spectrum_valid = 1;
                    g_gui_spectrum_seq += 1;
                    pthread_mutex_unlock(&g_gui_spectrum_mtx);
                    spec_inflight = false;
                }
            }

            if (!spec_inflight && spec_sample_acc >= spec_period_samples && spec_fft >= 64) {
                spec_sample_acc = 0;
                int threadsSpec = 128;
                int blocksSpec = (spec_bins + threadsSpec - 1) / threadsSpec;
                gpu_spectrum_bins_kernel<<<blocksSpec, threadsSpec, 0, spec_stream>>>(
                    d_iq_out, spec_fft, spec_bins, d_spec_db
                );
                cudaMemcpyAsync(spec_host, d_spec_db,
                                (size_t)spec_bins * sizeof(float),
                                cudaMemcpyDeviceToHost, spec_stream);
                cudaEventRecord(spec_done_evt, spec_stream);
                spec_inflight = true;
            }

            // 4. CPU 大老闆只需做最簡單的工作：將衛星狀態手動「快轉」 1 毫秒
            double dt_1ms = (double)samp_per_ms / fs;
            for(int c=0; c<n_ch; ++c) {
                // 快轉載波相位與頻率
                ch[c].carr_phase += CUDA_PI2 * (ch[c].fd + 0.5 * ch[c].fd_dot * dt_1ms) * dt_1ms;
                ch[c].fd += ch[c].fd_dot * dt_1ms;
                // 【修正】：使用 fmod 確保相位永遠嚴格收斂在 0 ~ 2*PI 之間，防止無限膨脹失去精度
                ch[c].carr_phase = fmod(ch[c].carr_phase, CUDA_PI2);
                if(ch[c].carr_phase < 0.0)  ch[c].carr_phase += CUDA_PI2;
                
                // 快轉測距碼相位與導航電文指標
                ch[c].code_phase += (ch[c].code_rate + 0.5 * ch[c].code_rate_dot * dt_1ms) * dt_1ms;
                ch[c].code_rate += ch[c].code_rate_dot * dt_1ms;
                ch[c].amp += ch[c].amp_dot * dt_1ms;

                while(ch[c].code_phase >= CUDA_CODE_LEN) {
                    ch[c].code_phase -= CUDA_CODE_LEN;
                    if(++ch[c].ms_count == 20) {
                        ch[c].ms_count = 0;
                        if(++ch[c].bit_ptr == 300) {
                            ch[c].bit_ptr = 0;
                            ch[c].sf_id = ch[c].sf_id % 5 + 1;
                            
                            // 👇 新增：在指標歸零的完美瞬間，更新下一幀 6 秒的導航電文 👇
                            ch[c].nav_sf_start += 6.0;
                            if (ch[c].nav_sf_start >= 604800.0) {
                                ch[c].nav_sf_start -= 604800.0;
                            }
                            
                            // 推算當下的發射週數 (扣除約 0.07 秒的傳播延遲)
                            int week_tx = (int)((g_t_tx - 0.07) / 604800.0);
                            if (ch[c].is_gps) {
                                double gps_sow_dummy = 0.0;
                                bdt_to_gpst_week_sow(week_tx,
                                                     (g_t_tx - 0.07) - (double)week_tx * 604800.0,
                                                     &week_tx,
                                                     &gps_sow_dummy);
                            }
                            
                            // 呼叫函式生成新的 300 bits，並直接覆寫到 ch[c].nav_bits
                            if (ch[c].is_gps) {
                                get_subframe_bits_gps(ch[c].prn, ch[c].sf_id, week_tx,
                                                      ch[c].nav_sf_start, 6.0, ch[c].nav_bits);
                            } else {
                                get_subframe_bits(ch[c].prn, ch[c].sf_id, week_tx,
                                                  ch[c].nav_sf_start, 6.0, ch[c].nav_bits);
                            }
                        }                          
                    }
                }
            }
            // =========================================================

            sample_count += samp_per_ms;
            
            /* 直接將 RAM 裡的 I/Q 陣列送進 USRP 天線！ */
            usrp_send(host_iq, samp_per_ms);
        }

        if (g_runtime_abort) break;

    }

    cudaEventDestroy(spec_done_evt);
    cudaStreamDestroy(spec_stream);

    cudaFree(d_spec_db);
    cudaFreeHost(host_iq);
    cudaFree(d_iq_out);
    cudaFree(d_channels);

    if (g_runtime_abort) {
        g_sig_state.valid = false;
        free_path(&path);
        return;
    }

    g_sig_state.valid = true;
    g_sig_state.n_ch = n_ch;
    memcpy(g_sig_state.ch, ch, (size_t)n_ch * sizeof(channel_t));
    g_sig_state.fs = cfg->fs;
    g_sig_state.single_prn = cfg->single_prn;
    g_sig_state.meo_only = cfg->meo_only;
    g_sig_state.prn37_only = cfg->prn37_only;
    g_sig_state.signal_mode = cfg->signal_mode;
    g_sig_state.next_bdt = start_bdt + (double)total_ms / 1000.0;
    
    free_path(&path);
}
/* ---------------------------  End  ------------------------------*/

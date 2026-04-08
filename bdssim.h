#ifndef BDSSIM_H
#define BDSSIM_H

#include <stdint.h>
#include <stdbool.h>
#include "coord.h"
#include "channel.h"
#include "globals.h"
#include "rinex.h"

extern bds_ephemeris_t eph[MAX_SAT];
extern gps_ephemeris_t gps_eph[GPS_EPH_SLOTS];
extern uint8_t     prn_code[MAX_SAT][CODE_LEN];
extern uint8_t     gps_prn_code[GPS_EPH_SLOTS][GPS_CA_LEN];

#define SIG_MODE_BDS 0
#define SIG_MODE_GPS 1
#define SIG_MODE_MIXED 2

typedef struct {
    char     rinex_file_bds[256];
    char     rinex_file_gps[256];
    char     time_start[32];
    double   llh[3];
    char     path_file[256];
    int      path_type;
    uint32_t duration;
    uint32_t step_ms;
    double   gain;         /* 信號增益 */
    double   target_cn0;
    unsigned seed;
    bool     byte_output;
    double   fs;           /* sample rate (Hz) */
    bool     meo_only;
    int      single_prn;
    bool     prn37_only;
    bool     signal_gps; /* true: GPS L1 C/A, false: BDS B1I */
    uint8_t  signal_mode; /* SIG_MODE_BDS / SIG_MODE_GPS / SIG_MODE_MIXED */
    bool     interference_mode; /* true: generate random-sequence BPSK interference */
    int      interference_selection; /* -1: none, 0: spoof/general, 1: jam */
    bool     iono_on;
    double   tx_gain;      /* USRP 發射增益 (0-31.5 dB) */
    bool     usrp_external_clk; /* true: external ref clock, false: internal */
    bool     print_ch_info; /* 是否輸出每顆衛星 rdot/fd */
    int      max_ch;       /* Maximum visible channels (1-16) */
} sim_config_t;

bool init_simulator(sim_config_t *, double start_bdt);
bool reload_simulator_nav(sim_config_t *, double start_bdt);
void generate_signal(const sim_config_t *cfg);
void reset_signal_engine_state(void);
int  cuda_runtime_smoke_test(void);
void cleanup_simulator(void);
int  select_channels(channel_t *ch, int *n_ch, const coord_t *usr,
                     int single_prn, bool meo_only, bool prn37_only,
                     int signal_mode, int max_ch);

#endif /* BDSSIM_H */

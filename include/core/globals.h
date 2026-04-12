/* globals.h */
#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdint.h>
#include <pthread.h>
#include "rinex.h"

/* Some platforms do not define M_PI in math.h */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Physical and signal constants */
#define CLIGHT      299792458.0    /* Speed of light (m/s) */
#define F_B1I       1561.098e6     /* B1I carrier frequency (Hz) */
#define F_GPS_L1    1575.42e6      /* GPS L1 carrier frequency (Hz) */
#define CHIPRATE    2.046e6        /* B1I code chipping rate (Hz) */
#define GPS_CA_CHIPRATE 1.023e6    /* GPS L1 C/A chip rate (Hz) */

/* Multi-sequence BPSK interference parameters */
#define N_SEQS_PER_SYS  6           /* Number of sequences per system (BDS/GPS) - increased for better coverage */
#define FREQ_OFFSET_HZ  500.0       /* Frequency offset between sequences (Hz) - wider spacing */
#define JAM_AMP_SCALE   1.5         /* Jamming amplitude scale factor (>1.0 increases power) */

#define BDS_B1I_IF_OFFSET_HZ (-7.161e6)   /* Legacy/default BDS IF shift */
#define GPS_L1_IF_OFFSET_HZ (+7.161e6)    /* Legacy/default GPS IF shift */
#define BDS_USRP_CENTER_HZ   1568.259e6    /* Legacy/default center (mixed profile) */
#define BDS_USRP_FS_HZ       20.8e6        /* Legacy/default sample rate (mixed profile) */

/* RF profiles by signal mode */
#define RF_FS_STEP_HZ            2.6e6
#define RF_BDS_ONLY_CENTER_HZ    F_B1I
#define RF_BDS_ONLY_MIN_FS_HZ    5.2e6
#define RF_GPS_ONLY_CENTER_HZ    F_GPS_L1
#define RF_GPS_ONLY_MIN_FS_HZ    2.6e6
#define RF_MIXED_CENTER_HZ       1568.259e6
#define RF_MIXED_MIN_FS_HZ       20.8e6
#define RF_BDS_ONLY_IF_OFFSET_HZ 0.0
#define RF_GPS_ONLY_IF_OFFSET_HZ 0.0
#define RF_MIXED_BDS_IF_OFFSET_HZ (-7.161e6)
#define RF_MIXED_GPS_IF_OFFSET_HZ (+7.161e6)

/* Simulation limits */
#define MAX_CH      16              /* Maximum channels */
#define CODE_LEN    2046           /* PRN code length */
#define MAX_SAT     65             /* PRN slots */
#define GPS_CA_LEN  1023           /* GPS L1 C/A code length */

/* Amplitude and sampling parameters */
#define FS_OUTPUT_HZ    5200000.0
#define FS_BYTE_HZ      25000000.0
#define CN0_TARGET_DBHZ 42.0
#define HEADROOM_RATIO  0.80
#define AMP_SMOOTH_TC_MS 1000
#define GAIN_MEO_DB     +0.5
#define GAIN_IGSO_DB    +1.5
#define GUI_SPECTRUM_BINS 512
#define GUI_SPEC_CENTER_MHZ 1561.098
#define GUI_SPEC_HALFSPAN_MHZ 2.6
#define GUI_SPEC_FFT_POINTS 1024
#define GUI_SPEC_FPS 15
#define GUI_TIME_MON_SAMPLES 1024

/* G2 tap table for B1I PRN generation */
static const uint8_t g2_taps[64][3] = {
 {0,0,0},
 {1,3,0},{1,4,0},{1,5,0},{1,6,0},{1,8,0},
 {1,9,0},{1,10,0},{1,11,0},{2,7,0},{3,4,0},
 {3,5,0},{3,6,0},{3,8,0},{3,9,0},{3,10,0},{3,11,0},
 {4,5,0},{4,6,0},{4,8,0},{4,9,0},{4,10,0},{4,11,0},
 {5,6,0},{5,8,0},{5,9,0},{5,10,0},{5,11,0},
 {6,8,0},{6,9,0},{6,10,0},{6,11,0},
 {8,9,0},{8,10,0},{8,11,0},
 {9,10,0},{9,11,0},{10,11,0},
 {1,2,7},{1,3,4},{1,3,6},{1,3,8},{1,3,10},
 {1,3,11},{1,4,5},{1,4,9},{1,5,6},{1,5,8},
 {1,5,10},{1,5,11},{1,6,9},{1,8,9},{1,9,10},{1,9,11},
 {2,3,7},{2,5,7},{2,7,9},{3,4,5},{3,4,9},
 {3,5,6},{3,5,8},{3,5,10},{3,5,11},{3,6,9}
};

/* GPS L1 C/A G2 phase selector taps, index 0 is unused */
static const uint8_t gps_ca_taps[GPS_EPH_SLOTS][2] = {
 {0,0},
 {2,6},{3,7},{4,8},{5,9},{1,9},{2,10},{1,8},{2,9},
 {3,10},{2,3},{3,4},{5,6},{6,7},{7,8},{8,9},{9,10},
 {1,4},{2,5},{3,6},{4,7},{5,8},{6,9},{1,3},{4,6},
 {5,7},{6,8},{7,9},{8,10},{1,6},{2,7},{3,8},{4,9}
};

extern int simulator_inited;
extern int prn_max;
extern double nav_time_min;
extern double nav_time_max;
extern int nav_week;
extern double iono_alpha[4];
extern double iono_beta[4];
extern int utc_bdt_diff;
extern int utc_gpst_diff;
extern double g_t_tx;
extern double g_target_cn0;
extern int g_enable_iono;
extern volatile int g_runtime_abort;
extern int g_active_prn_mask[MAX_SAT];
extern double g_receiver_lat_deg;
extern double g_receiver_lon_deg;
extern int g_receiver_valid;
extern double g_bds_if_offset_hz;
extern double g_gps_if_offset_hz;
extern float g_gui_spectrum_db[GUI_SPECTRUM_BINS];
extern int g_gui_spectrum_bins;
extern int g_gui_spectrum_valid;
extern uint64_t g_gui_spectrum_seq;
extern pthread_mutex_t g_gui_spectrum_mtx;
extern int16_t g_gui_time_iq[2 * GUI_TIME_MON_SAMPLES];
extern int g_gui_time_samples;
extern int g_gui_time_valid;

extern uint8_t gps_prn_code[GPS_EPH_SLOTS][GPS_CA_LEN];

void gps_l1_ca_generate(int prn, uint8_t *dst);
void init_prn_tables(void);

#endif /* GLOBALS_H */


#ifndef NAVBITS_H
#define NAVBITS_H

#include <stdint.h>

#define SUBFRAME_BITS      300
#define HALF_SUBFRAME_BITS (SUBFRAME_BITS / 2)
#define SF_STREAM_LEN      SUBFRAME_BITS

typedef struct {
    uint32_t sow;
    uint32_t satH1;
    uint32_t aodc;
    uint32_t urai;
    uint32_t toc;
    int32_t  a0;
    int32_t  a1;
    int32_t  a2;
    int32_t  tgd1;
    int32_t  tgd2;
    int32_t  alpha[4];
    int32_t  beta[4];
    int32_t  delta_n;
    int32_t  M0;
    uint32_t e;
    uint32_t sqrtA;
    int32_t  cuc;
    int32_t  cus;
    int32_t  crc;
    int32_t  crs;
    uint32_t toe;
    uint32_t aode;
    int32_t  i0;
    int32_t  omega0;
    int32_t  omega;
    int32_t  omegadot;
    int32_t  idot;
    int32_t  cic;
    int32_t  cis;
} B1I_D1_Frame;

typedef struct {
    uint32_t wn;
    uint32_t toe;
    uint32_t toc;
    uint32_t iode;
    uint32_t iodc;
    int32_t  deltan;
    int32_t  cuc;
    int32_t  cus;
    int32_t  cic;
    int32_t  cis;
    int32_t  crc;
    int32_t  crs;
    uint32_t ecc;
    uint32_t sqrta;
    int32_t  m0;
    int32_t  omg0;
    int32_t  inc0;
    int32_t  aop;
    int32_t  omgdot;
    int32_t  idot;
    int32_t  af0;
    int32_t  af1;
    int32_t  af2;
    int32_t  tgd;
    uint32_t svhlth;
    uint32_t codeL2;
    uint32_t ura;
    uint32_t wna;
    uint32_t toa;
    uint32_t dataId;
    uint32_t sbf4_page25_svId;
    uint32_t sbf5_page25_svId;
} GPS_L1CA_Frame;

static const double a_scale[4] = {
    1.0/(double)(1<<30),
    1.0/(double)(1<<27),
    1.0/(double)(1<<24),
    1.0/(double)(1<<24)
};
static const double b_scale[4] = {
    (double)(1<<11),
    (double)(1<<14),
    (double)(1<<16),
    (double)(1<<16)
};

void get_subframe_bits(int prn, int sf_id, int week, double sow,
                       double frame_len, uint8_t *out);
void get_subframe_bits_gps(int prn, int sf_id, int week, double sow,
                           double frame_len, uint8_t *out);
void print_ephemeris_params(int prn);

#endif /* NAVBITS_H */







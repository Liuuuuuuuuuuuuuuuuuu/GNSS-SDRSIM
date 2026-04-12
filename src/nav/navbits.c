/* navbits.c : 產生 B1I D1 子幀 (星曆) */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include "bdssim.h"
#include "navbits.h"
#include "bch.h"
#include "globals.h"

static inline double rad_to_sc(double v){return v/M_PI;}

/* Construct a 30-bit navigation word. "bits" is either 26 for the
 * first word type or 22 for the remaining words. */
static uint32_t build_word(uint32_t payload, int bits)
{
    if(bits == 22)
        return bch_interleave_22bit(payload);
    else if(bits == 26)
        return bch_encode_26bit(payload);
    return 0;
}

extern bds_ephemeris_t eph[MAX_SAT];

/* Convert bds_ephemeris_t to the simplified B1I_D1_Frame structure.
 * Only the fields needed for navigation message assembly are
 * populated here. */
extern double iono_alpha[4];
extern double iono_beta[4];

static void frame_from_ephemeris(const bds_ephemeris_t *e, B1I_D1_Frame *f)
{
    memset(f, 0, sizeof(*f));
    f->toc      = (uint32_t)(e->toc / 8);
    f->aodc     = e->aodc & 0x1F;
    f->urai     = 0;
    f->satH1    = 0;                  /* ignore ephemeris health */

    /* TGD values: 0.1 ns resolution (ICD 5.2.4.10) */
    f->tgd1     = (int32_t)llround(e->tgd1 / 1e-10);
    f->tgd2     = (int32_t)llround(e->tgd2 / 1e-10);

    f->a0       = (int32_t)llround(e->af0 / pow(2, -33));
    f->a1       = (int32_t)llround(e->af1 / pow(2, -50));
    f->a2       = (int32_t)llround(e->af2 / pow(2, -66));

    f->toe      = (uint32_t)(e->toe / 8);
    f->sqrtA    = (uint32_t)llround(e->sqrtA / pow(2, -19));
    f->e        = (uint32_t)llround(e->e / pow(2, -33));
    
    f->delta_n  = (int32_t)llround(rad_to_sc(e->deltan) / pow(2, -43));
    f->M0       = (int32_t)llround(rad_to_sc(e->M0) / pow(2, -31));

    f->omega0   = (int32_t)llround(rad_to_sc(e->omega0) / pow(2, -31));
    f->i0       = (int32_t)llround(rad_to_sc(e->i0) / pow(2, -31));
    f->omega    = (int32_t)llround(rad_to_sc(e->w) / pow(2, -31));
    f->crc      = (int32_t)llround(e->crc / pow(2, -6));
    f->crs      = (int32_t)llround(e->crs / pow(2, -6));
    f->cuc      = (int32_t)llround(e->cuc / pow(2, -31));
    f->cus      = (int32_t)llround(e->cus / pow(2, -31));
    f->cic      = (int32_t)llround(e->cic / pow(2, -31));
    f->cis      = (int32_t)llround(e->cis / pow(2, -31));
    f->aode     = e->aode & 0x1F;
    f->idot     = (int32_t)llround(rad_to_sc(e->idot) / pow(2, -43));
    f->omegadot = (int32_t)llround(rad_to_sc(e->omegadot) / pow(2, -43));
    
    /* Ionospheric parameters from header.  Each coefficient uses
     * a different scaling factor according to the B1I ICD. */
    for (int i = 0; i < 4; ++i) {
        f->alpha[i] = (int32_t)llround(iono_alpha[i] / a_scale[i]);
        f->beta[i]  = (int32_t)llround(iono_beta[i]  / b_scale[i]);
    }
}

/* --------------------------- 宏與工具 ------------------------------*/

/* 填 30-bit word 至 bit 流。pos 為 bit index 0..299 (MSB first) */
static void put_word(uint8_t *b, int pos, uint32_t w30)
{
    for(int i=0;i<30;i++)
        b[pos+i] = (w30>>(29-i)) & 1;
}

/* --------------------------- 子幀 1 ------------------------------*/
static void build_subframe1_d1(uint8_t *out, const B1I_D1_Frame *f, int week, double sow, double frame_len)
{
    memset(out,0,SF_STREAM_LEN);

    /* word1：幀同步 11 bits（11100010010） + FraID(001) + SOW[19:12] */
    uint32_t info = 0x712;            /* 11-bit preamble */
    info = (info << 4);               /* reserved bits */
    info = (info << 3) | 0x1;         /* FraID=001 */
    /*
     * SOW should reflect the start time of this subframe.
     * According to the B1I ICD section 5.2.4.3 the value
     * corresponds to the rising edge of the frame sync
     * at the beginning of the subframe.
     */
    uint32_t sow_int = (uint32_t)(floor(sow/frame_len)*frame_len);
    info = (info<<8) | ((sow_int>>12) & 0xFF);
    put_word(out,0, build_word(info, 26));

    /* word2: SOW[11:0] + SatH1 + AODC + URAI */
    uint32_t w2 = ((sow_int & 0xFFF) << 10) | ((f->satH1 & 1) << 9) | ((f->aodc & 0x1F) << 4) | (f->urai & 0xF);
    put_word(out,30, build_word(w2,22));

    /* word3: WN + toc high 9 bits */
    uint32_t w3 = ((week & 0x1FFF) << 9) | ((f->toc >> 8) & 0x1FF);
    put_word(out,60, build_word(w3,22));

    /* word4: toc low 8 + TGD1 + TGD2 high 4 */
    uint32_t w4 = ((f->toc & 0xFF) << 14) | ((f->tgd1 & 0x3FF) << 4) | ((f->tgd2 >> 6) & 0xF);
    put_word(out,90, build_word(w4,22));

    /* word5: TGD2 low 6 + alpha0 + alpha1 */
    uint32_t w5 = ((f->tgd2 & 0x3F) << 16) | ((f->alpha[0] & 0xFF) << 8) | (f->alpha[1] & 0xFF);
    put_word(out,120, build_word(w5,22));

    /* word6: alpha2 + alpha3 + beta0 high 6 */
    uint32_t w6 = ((f->alpha[2] & 0xFF) << 14) | ((f->alpha[3] & 0xFF) << 6) | ((f->beta[0] >> 2) & 0x3F);
    put_word(out,150, build_word(w6,22));

    /* word7: beta0 low 2 + beta1 + beta2 + beta3 high 4 */
    uint32_t w7 = ((f->beta[0] & 0x3) << 20) | ((f->beta[1] & 0xFF) << 12) | ((f->beta[2] & 0xFF) << 4) | ((f->beta[3] >> 4) & 0xF);
    put_word(out,180, build_word(w7,22));

    /* word8: beta3 low 4 + a2 + a0 high 7 */
    uint32_t w8 = ((f->beta[3] & 0xF) << 18) | ((f->a2 & 0x7FF) << 7) | ((f->a0 >> 17) & 0x7F);
    put_word(out,210, build_word(w8,22));

    /* word9: a0 low 17 + a1 high 5 */
    uint32_t w9 = ((f->a0 & 0x1FFFF) << 5) | ((f->a1 >> 17) & 0x1F);
    put_word(out,240, build_word(w9,22));

    /* word10: a1 low 17 + AODE */
    uint32_t w10 = ((f->a1 & 0x1FFFF) << 5) | (f->aode & 0x1F);
    put_word(out,270, build_word(w10,22));
}

/* --------------------------- 子幀 2 ------------------------------*/
static void build_subframe2_d1(uint8_t *out, const B1I_D1_Frame *f, double sow, double frame_len)
{
    memset(out, 0, SF_STREAM_LEN);

    uint32_t sow_int = (uint32_t)(floor(sow / frame_len) * frame_len);
    uint32_t w;

    /* Word1: preamble + reserved + FraID=2 + SOW[19:12] */
    w = 0x712;
    w = (w << 4);
    w = (w << 3) | 2;
    w = (w << 8) | ((sow_int >> 12) & 0xFF);
    put_word(out, 0, build_word(w, 26));

    /* Word2: SOW[11:0] + delta_n high 10 bits */
    uint32_t w2 = ((sow_int & 0xFFF) << 10) | ((uint32_t)f->delta_n >> 6 & 0x3FF);
    put_word(out, 30, build_word(w2, 22));

    /* Word3: delta_n low 6 + Cuc high 16 */
    uint32_t w3 = ((uint32_t)f->delta_n & 0x3F) << 16 | ((uint32_t)f->cuc >> 2 & 0xFFFF);
    put_word(out, 60, build_word(w3, 22));

    /* Word4: Cuc low 2 + M0 high 20 */
    uint32_t w4 = ((uint32_t)f->cuc & 0x3) << 20 | ((uint32_t)f->M0 >> 12 & 0xFFFFF);
    put_word(out, 90, build_word(w4, 22));

    /* Word5: M0 low 12 + e high 10 */
    uint32_t w5 = ((uint32_t)f->M0 & 0xFFF) << 10 | ((uint32_t)f->e >> 22 & 0x3FF);
    put_word(out, 120, build_word(w5, 22));

    /* Word6: e low 22 */
    uint32_t w6 = (uint32_t)f->e & 0x3FFFFF;
    put_word(out, 150, build_word(w6, 22));

    /* Word7: Cus + Crc high 4 */
    uint32_t w7 = ((uint32_t)f->cus & 0x3FFFF) << 4 | ((uint32_t)f->crc >> 14 & 0xF);
    put_word(out, 180, build_word(w7, 22));

    /* Word8: Crc low 14 + Crs high 8 */
    uint32_t w8 = ((uint32_t)f->crc & 0x3FFF) << 8 | ((uint32_t)f->crs >> 10 & 0xFF);
    put_word(out, 210, build_word(w8, 22));

    /* Word9: Crs low 10 + sqrtA high 12 */
    uint32_t w9 = ((uint32_t)f->crs & 0x3FF) << 12 | ((uint32_t)f->sqrtA >> 20 & 0xFFF);
    put_word(out, 240, build_word(w9, 22));

    /* Word10: sqrtA low 20 + toe high 2 */
    uint32_t w10 = ((uint32_t)f->sqrtA & 0xFFFFF) << 2 | ((f->toe >> 15) & 0x3);
    put_word(out, 270, build_word(w10, 22));
}

/* --------------------------- 子幀 3 ------------------------------*/
static void build_subframe3_d1(uint8_t *out, const B1I_D1_Frame *f, double sow, double frame_len)
{
    memset(out, 0, SF_STREAM_LEN);

    uint32_t sow_int = (uint32_t)(floor(sow / frame_len) * frame_len);
    uint32_t w;

    /* Word1: preamble + reserved + FraID=3 + SOW[19:12] */
    w = 0x712;
    w = (w << 4);
    w = (w << 3) | 3;
    w = (w << 8) | ((sow_int >> 12) & 0xFF);
    put_word(out, 0, build_word(w, 26));

    /* Word2: SOW[11:0] + toe bits[14:5] */
    uint32_t w2 = ((sow_int & 0xFFF) << 10) | ((f->toe >> 5) & 0x3FF);
    put_word(out, 30, build_word(w2, 22));

    /* Word3: toe low5 + i0 high17 */
    uint32_t w3 = ((f->toe & 0x1F) << 17) | ((uint32_t)f->i0 >> 15 & 0x1FFFF);
    put_word(out, 60, build_word(w3, 22));

    /* Word4: i0 low15 + Cic high7 */
    uint32_t w4 = ((uint32_t)f->i0 & 0x7FFF) << 7 | ((uint32_t)f->cic >> 11 & 0x7F);
    put_word(out, 90, build_word(w4, 22));

    /* Word5: Cic low11 + omegadot high11 */
    uint32_t w5 = ((uint32_t)f->cic & 0x7FF) << 11 | ((uint32_t)f->omegadot >> 13 & 0x7FF);
    put_word(out, 120, build_word(w5, 22));

    /* Word6: omegadot low13 + Cis high9 */
    uint32_t w6 = ((uint32_t)f->omegadot & 0x1FFF) << 9 | ((uint32_t)f->cis >> 9 & 0x1FF);
    put_word(out, 150, build_word(w6, 22));

    /* Word7: Cis low9 + IDOT high13 */
    uint32_t w7 = ((uint32_t)f->cis & 0x1FF) << 13 | ((uint32_t)f->idot >> 1 & 0x1FFF);
    put_word(out, 180, build_word(w7, 22));

    /* Word8: IDOT low1 + omega0 high21 */
    uint32_t w8 = ((uint32_t)f->idot & 0x1) << 21 | ((uint32_t)f->omega0 >> 11 & 0x1FFFFF);
    put_word(out, 210, build_word(w8, 22));

    /* Word9: omega0 low11 + omega high11 */
    uint32_t w9 = ((uint32_t)f->omega0 & 0x7FF) << 11 | ((uint32_t)f->omega >> 21 & 0x7FF);
    put_word(out, 240, build_word(w9, 22));

    /* Word10: omega low21 + reserved bit */
    uint32_t w10 = ((uint32_t)f->omega & 0x1FFFFF) << 1;
    put_word(out, 270, build_word(w10, 22));
}

/* --------------------------- 子幀 4 ------------------------------*/
static void build_subframe4_d1(uint8_t *out, double sow, double frame_len)
{
    /* Subframe 4 carries almanac pages.  Actual parameters are not yet
     * implemented, so we keep the word layout but fill the data portion
     * with the official "10" placeholder pattern (0x2AAAAA). */

    memset(out, 0, SF_STREAM_LEN);

    uint32_t sow_int = (uint32_t)(floor(sow / frame_len) * frame_len);

    /* word1: preamble + reserved + FraID=4 + SOW[19:12] */
    uint32_t w = 0x712;
    w = (w << 4);
    w = (w << 3) | 4;
    w = (w << 8) | ((sow_int >> 12) & 0xFF);
    put_word(out, 0, build_word(w, 26));

    /* word2: SOW[11:0] + reserved + PNUM + spare (1,0) */
    int pnum = 1;
    uint32_t w2 = ((sow_int & 0xFFF) << 10) | (0 << 9) | ((pnum & 0x7F) << 2) | 0x2;
    put_word(out, 30, build_word(w2, 22));

    uint32_t filler = 0x2AAAAA; /* 1010... repeated */
    for (int i = 2; i < 9; ++i)
        put_word(out, i * 30, build_word(filler, 22));

    /* word10: spare + AmEpID (1,1) */
    uint32_t w10 = (0xAAAAA << 2) | 0x3;
    put_word(out, 270, build_word(w10, 22));
    
}
/* --------------------------- 子幀 5 ------------------------------*/
static void build_subframe5_d1(uint8_t *out, double sow, double frame_len)
{
    /* Identical to subframe 4 but with FraID=5.  Data words use the same
     * placeholder pattern since almanac/UTC parameters are not generated. */

    memset(out, 0, SF_STREAM_LEN);

    uint32_t sow_int = (uint32_t)(floor(sow / frame_len) * frame_len);

    /* word1: preamble + reserved + FraID=5 + SOW[19:12] */
    uint32_t w = 0x712;
    w = (w << 4);
    w = (w << 3) | 5;
    w = (w << 8) | ((sow_int >> 12) & 0xFF);
    put_word(out, 0, build_word(w, 26));

    /* word2: SOW[11:0] + reserved + PNUM + spare (1,0) */
    int pnum = 24;
    uint32_t w2 = ((sow_int & 0xFFF) << 10) | (0 << 9) | ((pnum & 0x7F) << 2) | 0x2;
    put_word(out, 30, build_word(w2, 22));

    uint32_t filler = 0x2AAAAA;
    for (int i = 2; i < 8; ++i)
        put_word(out, i * 30, build_word(filler, 22));

    /* word8: spare + AmID (0,0) + spare */
    uint32_t w8 = (0x15 << 17) | (0 << 2) | 0x5555;
    put_word(out, 210, build_word(w8, 22));

    put_word(out, 240, build_word(filler, 22));
    put_word(out, 270, build_word(filler, 22));
}

/* --------------------------- GPS L1 C/A 子幀 ------------------------------*/
static uint32_t gps_count_bits(uint32_t v)
{
    const int s[5] = {1, 2, 4, 8, 16};
    const uint32_t b[5] = {0x55555555u, 0x33333333u, 0x0F0F0F0Fu, 0x00FF00FFu, 0x0000FFFFu};
    uint32_t c = v;
    c = ((c >> s[0]) & b[0]) + (c & b[0]);
    c = ((c >> s[1]) & b[1]) + (c & b[1]);
    c = ((c >> s[2]) & b[2]) + (c & b[2]);
    c = ((c >> s[3]) & b[3]) + (c & b[3]);
    c = ((c >> s[4]) & b[4]) + (c & b[4]);
    return c;
}

static uint32_t gps_compute_checksum(uint32_t source, int nib)
{
    static const uint32_t bmask[6] = {
        0x3B1F3480u, 0x1D8F9A40u, 0x2EC7CD00u,
        0x1763E680u, 0x2BB1F340u, 0x0B7A89C0u
    };

    uint32_t d = source & 0x3FFFFFC0u;
    uint32_t d29 = (source >> 31) & 0x1u;
    uint32_t d30 = (source >> 30) & 0x1u;

    if (nib) {
        /* Word 2/10: solve D23,D24 so parity remains valid when they are non-information bits. */
        if (((d30 + gps_count_bits(bmask[4] & d)) & 1u) != 0u)
            d ^= (1u << 6);
        if (((d29 + gps_count_bits(bmask[5] & d)) & 1u) != 0u)
            d ^= (1u << 7);
    }

    uint32_t D = d;
    if (d30)
        D ^= 0x3FFFFFC0u;

    D |= ((d29 + gps_count_bits(bmask[0] & d)) & 1u) << 5;
    D |= ((d30 + gps_count_bits(bmask[1] & d)) & 1u) << 4;
    D |= ((d29 + gps_count_bits(bmask[2] & d)) & 1u) << 3;
    D |= ((d30 + gps_count_bits(bmask[3] & d)) & 1u) << 2;
    D |= ((d30 + gps_count_bits(bmask[4] & d)) & 1u) << 1;
    D |= ((d29 + gps_count_bits(bmask[5] & d)) & 1u);

    return D & 0x3FFFFFFFu;
}

typedef struct {
    int valid;
    long long frame_index;
    uint32_t last_word;
} gps_nav_state_t;

static gps_nav_state_t g_gps_nav_state[GPS_EPH_SLOTS];

static void gps_frame_from_ephemeris(const gps_ephemeris_t *e, int week,
                                     GPS_L1CA_Frame *f)
{
    if (!e || !f)
        return;

    memset(f, 0, sizeof(*f));
    f->wn = (uint32_t)(week & 0x3FF);
    f->toe = (uint32_t)llround(e->toe / 16.0) & 0xFFFFu;
    f->toc = (uint32_t)llround((double)e->toc / 16.0) & 0xFFFFu;
    f->iode = (uint32_t)(e->iode & 0xFF);
    f->iodc = (uint32_t)(e->iodc & 0x3FF);

    f->deltan = (int32_t)llround(rad_to_sc(e->deltan) / ldexp(1.0, -43));
    f->cuc = (int32_t)llround(e->cuc / ldexp(1.0, -29));
    f->cus = (int32_t)llround(e->cus / ldexp(1.0, -29));
    f->cic = (int32_t)llround(e->cic / ldexp(1.0, -29));
    f->cis = (int32_t)llround(e->cis / ldexp(1.0, -29));
    f->crc = (int32_t)llround(e->crc / ldexp(1.0, -5));
    f->crs = (int32_t)llround(e->crs / ldexp(1.0, -5));
    f->ecc = (uint32_t)llround(e->e / ldexp(1.0, -33));
    f->sqrta = (uint32_t)llround(e->sqrtA / ldexp(1.0, -19));
    f->m0 = (int32_t)llround(rad_to_sc(e->M0) / ldexp(1.0, -31));
    f->omg0 = (int32_t)llround(rad_to_sc(e->omega0) / ldexp(1.0, -31));
    f->inc0 = (int32_t)llround(rad_to_sc(e->i0) / ldexp(1.0, -31));
    f->aop = (int32_t)llround(rad_to_sc(e->w) / ldexp(1.0, -31));
    f->omgdot = (int32_t)llround(rad_to_sc(e->omegadot) / ldexp(1.0, -43));
    f->idot = (int32_t)llround(rad_to_sc(e->idot) / ldexp(1.0, -43));
    f->af0 = (int32_t)llround(e->af0 / ldexp(1.0, -31));
    f->af1 = (int32_t)llround(e->af1 / ldexp(1.0, -43));
    f->af2 = (int32_t)llround(e->af2 / ldexp(1.0, -55));
    f->tgd = (int32_t)llround(e->tgd / ldexp(1.0, -31));

    f->svhlth = (uint32_t)(e->health & 0x3F);
    f->codeL2 = (uint32_t)(e->codeL2 & 0x3);
    f->ura = (uint32_t)(e->ura & 0xF);
    f->wna = (uint32_t)(week & 0xFF);
    f->toa = (uint32_t)llround(e->toe / 4096.0) & 0xFF;
    f->dataId = 1u;
    f->sbf4_page25_svId = 63u;
    f->sbf5_page25_svId = 51u;
}

static void build_subframe1_l1ca(uint32_t sbf[10], const GPS_L1CA_Frame *f)
{
    sbf[0] = 0x8B0000u << 6;
    sbf[1] = 0x1u << 8;
    sbf[2] = ((f->wn & 0x3FFu) << 20) | ((f->codeL2 & 0x3u) << 18) | ((f->ura & 0xFu) << 14) |
             ((f->svhlth & 0x3Fu) << 8) | (((f->iodc >> 8) & 0x3u) << 6);
    sbf[3] = 0u;
    sbf[4] = 0u;
    sbf[5] = 0u;
    sbf[6] = ((uint32_t)f->tgd & 0xFFu) << 6;
    sbf[7] = ((f->iodc & 0xFFu) << 22) | ((f->toc & 0xFFFFu) << 6);
    sbf[8] = (((uint32_t)f->af2 & 0xFFu) << 22) | (((uint32_t)f->af1 & 0xFFFFu) << 6);
    sbf[9] = (((uint32_t)f->af0 & 0x3FFFFFu) << 8);
}

static void build_subframe2_l1ca(uint32_t sbf[10], const GPS_L1CA_Frame *f)
{
    sbf[0] = 0x8B0000u << 6;
    sbf[1] = 0x2u << 8;
    sbf[2] = ((f->iode & 0xFFu) << 22) | (((uint32_t)f->crs & 0xFFFFu) << 6);
    sbf[3] = (((uint32_t)f->deltan & 0xFFFFu) << 14) | ((((uint32_t)f->m0 >> 24) & 0xFFu) << 6);
    sbf[4] = (((uint32_t)f->m0 & 0xFFFFFFu) << 6);
    sbf[5] = (((uint32_t)f->cuc & 0xFFFFu) << 14) | (((f->ecc >> 24) & 0xFFu) << 6);
    sbf[6] = ((f->ecc & 0xFFFFFFu) << 6);
    sbf[7] = (((uint32_t)f->cus & 0xFFFFu) << 14) | (((f->sqrta >> 24) & 0xFFu) << 6);
    sbf[8] = ((f->sqrta & 0xFFFFFFu) << 6);
    sbf[9] = ((f->toe & 0xFFFFu) << 14);
}

static void build_subframe3_l1ca(uint32_t sbf[10], const GPS_L1CA_Frame *f)
{
    sbf[0] = 0x8B0000u << 6;
    sbf[1] = 0x3u << 8;
    sbf[2] = (((uint32_t)f->cic & 0xFFFFu) << 14) | ((((uint32_t)f->omg0 >> 24) & 0xFFu) << 6);
    sbf[3] = (((uint32_t)f->omg0 & 0xFFFFFFu) << 6);
    sbf[4] = (((uint32_t)f->cis & 0xFFFFu) << 14) | ((((uint32_t)f->inc0 >> 24) & 0xFFu) << 6);
    sbf[5] = (((uint32_t)f->inc0 & 0xFFFFFFu) << 6);
    sbf[6] = (((uint32_t)f->crc & 0xFFFFu) << 14) | ((((uint32_t)f->aop >> 24) & 0xFFu) << 6);
    sbf[7] = (((uint32_t)f->aop & 0xFFFFFFu) << 6);
    sbf[8] = (((uint32_t)f->omgdot & 0xFFFFFFu) << 6);
    sbf[9] = ((f->iode & 0xFFu) << 22) | (((uint32_t)f->idot & 0x3FFFu) << 8);
}

static void build_subframe4_l1ca(uint32_t sbf[10], const GPS_L1CA_Frame *f)
{
    sbf[0] = 0x8B0000u << 6;
    sbf[1] = 0x4u << 8;
    sbf[2] = (f->dataId << 28) | (f->sbf4_page25_svId << 22);
    sbf[3] = 0u;
    sbf[4] = 0u;
    sbf[5] = 0u;
    sbf[6] = 0u;
    sbf[7] = 0u;
    sbf[8] = 0u;
    sbf[9] = 0u;
}

static void build_subframe5_l1ca(uint32_t sbf[10], const GPS_L1CA_Frame *f)
{
    sbf[0] = 0x8B0000u << 6;
    sbf[1] = 0x5u << 8;
    sbf[2] = (f->dataId << 28) | (f->sbf5_page25_svId << 22) |
             ((f->toa & 0xFFu) << 14) | ((f->wna & 0xFFu) << 6);
    sbf[3] = 0u;
    sbf[4] = 0u;
    sbf[5] = 0u;
    sbf[6] = 0u;
    sbf[7] = 0u;
    sbf[8] = 0u;
    sbf[9] = 0u;
}

static uint32_t gps_prev_seed_for_frame(int prn, int week, double frame_start)
{
    long long frame_count = (long long)floor(frame_start / 30.0);
    long long frame_index = (long long)week * 20160LL + frame_count;
    gps_nav_state_t *st = &g_gps_nav_state[prn];

    if (st->valid && st->frame_index + 1 == frame_index)
        return st->last_word;

    return 0u;
}

static void gps_update_frame_state(int prn, int week, double frame_start,
                                   uint32_t last_word)
{
    long long frame_count = (long long)floor(frame_start / 30.0);
    long long frame_index = (long long)week * 20160LL + frame_count;
    gps_nav_state_t *st = &g_gps_nav_state[prn];
    st->valid = 1;
    st->frame_index = frame_index;
    st->last_word = last_word;
}

static void build_subframe_gps(int prn, int sf_id, int week, double sow,
                               const GPS_L1CA_Frame *f, uint8_t *out)
{
    memset(out, 0, SF_STREAM_LEN);

    if (!f || sf_id < 1 || sf_id > 5)
        return;

    double frame_start = floor(sow / 30.0) * 30.0;
    uint32_t prev_seed = gps_prev_seed_for_frame(prn, week, frame_start);
    uint32_t prevwrd = prev_seed;
    uint32_t tow0 = (uint32_t)floor(frame_start / 6.0);
    uint32_t sbf[10] = {0};
    uint32_t selected_words[10] = {0};

    for (int isbf = 1; isbf <= 5; ++isbf) {
        if (isbf == 1) {
            build_subframe1_l1ca(sbf, f);
        } else if (isbf == 2) {
            build_subframe2_l1ca(sbf, f);
        } else if (isbf == 3) {
            build_subframe3_l1ca(sbf, f);
        } else if (isbf == 4) {
            build_subframe4_l1ca(sbf, f);
        } else if (isbf == 5) {
            build_subframe5_l1ca(sbf, f);
        } else {
            memset(sbf, 0, sizeof(sbf));
        }

        for (int iwrd = 0; iwrd < 10; ++iwrd) {
            uint32_t sbfwrd = sbf[iwrd];

            if (iwrd == 1) {
                uint32_t tow = tow0 + (uint32_t)isbf;
                sbfwrd |= (tow & 0x1FFFFu) << 13;
            }

            sbfwrd |= (prevwrd << 30) & 0xC0000000u;
            {
                int nib = ((iwrd == 1) || (iwrd == 9)) ? 1 : 0;
                uint32_t outw = gps_compute_checksum(sbfwrd, nib);
                if (isbf == sf_id)
                    selected_words[iwrd] = outw;
                prevwrd = outw;
            }
        }
    }

    gps_update_frame_state(prn, week, frame_start, prevwrd);

    for (int iwrd = 0; iwrd < 10; ++iwrd)
        put_word(out, iwrd * 30, selected_words[iwrd]);
}

/* --------------------------- 根據時間取得子幀 bit 流 (300 bits) ------------------------------*/
void get_subframe_bits(int prn,int sf_id,int week,double sow,double frame_len,uint8_t *out)
{
    double start = floor(sow/frame_len)*frame_len;
    B1I_D1_Frame frm, *pf = NULL;
    if(sf_id >=1 && sf_id <=3){
        frame_from_ephemeris(&eph[prn], &frm);
        pf = &frm;
    }

    if(sf_id==1){
        build_subframe1_d1(out,pf,week,start,frame_len);
    }else if(sf_id==2){
        build_subframe2_d1(out,pf,start,frame_len);
    }else if(sf_id==3){
        build_subframe3_d1(out,pf,start,frame_len);
    }else if(sf_id==4){
        build_subframe4_d1(out,start,frame_len);
    }else if(sf_id==5){
        build_subframe5_d1(out,start,frame_len);
    }else{
        memset(out,0,SF_STREAM_LEN);
    }
}

void get_subframe_bits_gps(int prn,int sf_id,int week,double sow,double frame_len,uint8_t *out)
{
    (void)frame_len;

    int sid = sf_id;
    if (sid < 1 || sid > 5)
        sid = ((int)floor(sow / 6.0)) % 5 + 1;

    if (prn < 1 || prn >= GPS_EPH_SLOTS) {
        memset(out, 0, SF_STREAM_LEN);
        return;
    }

    const gps_ephemeris_t *e = &gps_eph[prn];
    if (!e || e->prn == 0) {
        memset(out, 0, SF_STREAM_LEN);
        return;
    }

    GPS_L1CA_Frame frame;
    gps_frame_from_ephemeris(e, week, &frame);
    build_subframe_gps(prn, sid, week, sow, &frame, out);
}

/* --------------------------- Dump broadcast ephemeris parameters for a given PRN ------------------------------*/
void print_ephemeris_params(int prn)
{
    B1I_D1_Frame f;
    frame_from_ephemeris(&eph[prn], &f);
    printf("[PRN %d] toc=%u aodc=%u aode=%u urai=%u satH1=%u\n",
           prn, f.toc, f.aodc, f.aode, f.urai, f.satH1);
    printf("[PRN %d] tgd1=%d tgd2=%d af0=%d af1=%d af2=%d\n",
           prn, f.tgd1, f.tgd2, f.a0, f.a1, f.a2);
    printf("[PRN %d] alpha=%d,%d,%d,%d beta=%d,%d,%d,%d\n",
           prn, f.alpha[0], f.alpha[1], f.alpha[2], f.alpha[3],
           f.beta[0], f.beta[1], f.beta[2], f.beta[3]);
    printf("[PRN %d] delta_n=%d M0=%d e=%u sqrtA=%u\n",
           prn, f.delta_n, f.M0, f.e, f.sqrtA);
    printf("[PRN %d] cuc=%d cus=%d crc=%d crs=%d\n",
           prn, f.cuc, f.cus, f.crc, f.crs);
    printf("[PRN %d] toe=%u i0=%d omega0=%d omega=%d\n",
           prn, f.toe, f.i0, f.omega0, f.omega);
    printf("[PRN %d] omegadot=%d idot=%d cic=%d cis=%d\n",
           prn, f.omegadot, f.idot, f.cic, f.cis);
}
/* ---------------------------  End  ------------------------------*/

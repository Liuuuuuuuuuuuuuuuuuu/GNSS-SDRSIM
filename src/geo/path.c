#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "path.h"

static void trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    while (*s && isspace((unsigned char)*s))
        memmove(s, s + 1, e - s + 1);
}

static int count_lines(FILE *fp)
{
    int n = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp))
        if (buf[0] && buf[0] != '#' && buf[0] != '\n')
            n++;
    fseek(fp, 0, SEEK_SET);
    return n;
}
typedef int (*parse_fn)(const char *buf, coord_t *out);

static int parse_xyz(const char *buf, coord_t *out)
{
    double x, y, z;
    if (sscanf(buf, "%lf%lf%lf", &x, &y, &z) != 3)
        return -1;
    memset(out, 0, sizeof(*out));
    out->xyz[0] = x;
    out->xyz[1] = y;
    out->xyz[2] = z;
    return 0;
}

static int parse_llh(const char *buf, coord_t *out)
{
    double lat, lon, h;
    if (sscanf(buf, "%lf%lf%lf", &lat, &lon, &h) != 3)
        return -1;
    lat *= M_PI/180.0;
    lon *= M_PI/180.0;
    lla_to_ecef((double[3]){lat, lon, h}, out);
    return 0;
}

static int parse_nmea_gga(const char *buf, coord_t *out)
{
    double rawlat = 0.0, rawlon = 0.0, alt = 0.0;
    char ns = 'N', ew = 'E';
    int n = sscanf(buf,
                   "$%*[^,],%*[^,],%lf,%c,%lf,%c,%*[^,],%*[^,],%*[^,],%lf",
                   &rawlat, &ns, &rawlon, &ew, &alt);
    if (n < 5)
        return -1;

    int d = (int)(rawlat / 100);
    double lat = d + (rawlat - d * 100) / 60.0;
    if (ns == 'S')
        lat = -lat;

    d = (int)(rawlon / 100);
    double lon = d + (rawlon - d * 100) / 60.0;
    if (ew == 'W')
        lon = -lon;

    lat *= M_PI/180.0;
    lon *= M_PI/180.0;
    lla_to_ecef((double[3]){lat, lon, alt}, out);
    return 0;
}

static int load_path_common(const char *file, path_t *path, parse_fn parse)
{
    FILE *fp = fopen(file, "r");
    if (!fp)
        return -1;

    int n = count_lines(fp);
    path->p = malloc(sizeof(*path->p) * n);
    path->n = 0;

    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        trim(buf);
        if (buf[0] == '#' || buf[0] == '\0')
            continue;
        coord_t c;
        if (parse(buf, &c) == 0)
            path->p[path->n++] = c;
    }

    fclose(fp);
    return path->n == n ? 0 : -1;
}

int load_path_xyz(const char *file, path_t *path)
{
    return load_path_common(file, path, parse_xyz);
}

int load_path_llh(const char *file, path_t *path)
{
    return load_path_common(file, path, parse_llh);
}

int load_path_nmea(const char *file, path_t *path)
{
    return load_path_common(file, path, parse_nmea_gga);
}

void free_path(path_t *path)
{
    if (path->p) {
        free(path->p);
        path->p = NULL;
    }
    path->n = 0;
}

void interpolate_path(const path_t *path, double t, coord_t *out)
{
    if (!path->p || path->n == 0) {
        memset(out, 0, sizeof(coord_t));
        return;
    }

    if (t <= 0) {
        *out = path->p[0];
        return;
    }

    double ti = t * PATH_UPDATE_HZ;
    int i = (int)ti;
    if (i >= path->n - 1) {
        *out = path->p[path->n - 1];
        return;
    }

    double f = ti - i;
    for (int k = 0; k < 3; ++k)
        out->xyz[k] = path->p[i].xyz[k] * (1.0 - f) +
                      path->p[i + 1].xyz[k] * f;
}

void interpolate_path_llh(const path_t *path,double t,double llh[3])
{
    if(!path->p || path->n==0){
        llh[0]=llh[1]=llh[2]=0.0;
        return;
    }
    if(t<=0){
        memcpy(llh,path->p[0].llh,sizeof(double)*3);
        return;
    }
    double ti=t * PATH_UPDATE_HZ;
    int i=(int)ti;
    if(i>=path->n-1){
        memcpy(llh,path->p[path->n-1].llh,sizeof(double)*3);
        return;
    }
    double f=ti-i;
    for(int k=0;k<3;++k)
        llh[k]=path->p[i].llh[k]*(1.0-f)+path->p[i+1].llh[k]*f;
}

static int clamp_index(int idx, int n)
{
    if (idx < 0)
        return 0;
    if (idx >= n)
        return n - 1;
    return idx;
}

void path_clear_anchors(path_t *path)
{
    if (!path)
        return;
    path->has_prev_anchor = 0;
    path->has_next_anchor = 0;
    path->prev_anchor_xyz[0] = path->prev_anchor_xyz[1] = path->prev_anchor_xyz[2] = 0.0;
    path->next_anchor_xyz[0] = path->next_anchor_xyz[1] = path->next_anchor_xyz[2] = 0.0;
}

void path_set_prev_anchor_xyz(path_t *path, const double xyz[3])
{
    if (!path || !xyz)
        return;
    path->has_prev_anchor = 1;
    path->prev_anchor_xyz[0] = xyz[0];
    path->prev_anchor_xyz[1] = xyz[1];
    path->prev_anchor_xyz[2] = xyz[2];
}

void path_set_next_anchor_xyz(path_t *path, const double xyz[3])
{
    if (!path || !xyz)
        return;
    path->has_next_anchor = 1;
    path->next_anchor_xyz[0] = xyz[0];
    path->next_anchor_xyz[1] = xyz[1];
    path->next_anchor_xyz[2] = xyz[2];
}

static void sample_point_xyz(const path_t *path, int idx, int is_llh, double out_xyz[3])
{
    if (idx < 0 && path->has_prev_anchor) {
        out_xyz[0] = path->prev_anchor_xyz[0];
        out_xyz[1] = path->prev_anchor_xyz[1];
        out_xyz[2] = path->prev_anchor_xyz[2];
        return;
    }
    if (idx >= path->n && path->has_next_anchor) {
        out_xyz[0] = path->next_anchor_xyz[0];
        out_xyz[1] = path->next_anchor_xyz[1];
        out_xyz[2] = path->next_anchor_xyz[2];
        return;
    }

    idx = clamp_index(idx, path->n);
    if (is_llh) {
        coord_t tmp = {0};
        lla_to_ecef(path->p[idx].llh, &tmp);
        out_xyz[0] = tmp.xyz[0];
        out_xyz[1] = tmp.xyz[1];
        out_xyz[2] = tmp.xyz[2];
        return;
    }

    out_xyz[0] = path->p[idx].xyz[0];
    out_xyz[1] = path->p[idx].xyz[1];
    out_xyz[2] = path->p[idx].xyz[2];
}

void interpolate_path_kinematic(const path_t *path, double t,
                                coord_t *out_pos, double out_vel[3],
                                int is_llh)
{
    if (!out_pos || !out_vel) {
        return;
    }

    if (!path || !path->p || path->n <= 0) {
        memset(out_pos, 0, sizeof(*out_pos));
        out_vel[0] = out_vel[1] = out_vel[2] = 0.0;
        return;
    }

    if (path->n == 1) {
        memset(out_pos, 0, sizeof(*out_pos));
        sample_point_xyz(path, 0, is_llh, out_pos->xyz);
        if (is_llh)
            ecef_to_lla(out_pos->xyz, out_pos);
        out_vel[0] = out_vel[1] = out_vel[2] = 0.0;
        return;
    }

    double tc = t * PATH_UPDATE_HZ;
    if (tc < 0.0)
        tc = 0.0;
    if (tc > (double)(path->n - 1))
        tc = (double)(path->n - 1);

    int i = (int)floor(tc);
    double s = tc - (double)i;
    if (i >= path->n - 1) {
        i = path->n - 2;
        s = 1.0;
    }

    double P0[3], P1[3], P2[3], P3[3];
    sample_point_xyz(path, i - 1, is_llh, P0);
    sample_point_xyz(path, i,     is_llh, P1);
    sample_point_xyz(path, i + 1, is_llh, P2);
    sample_point_xyz(path, i + 2, is_llh, P3);

    for (int k = 0; k < 3; ++k) {
        const double v0 = P0[k];
        const double v1 = P1[k];
        const double v2 = P2[k];
        const double v3 = P3[k];
        const double f = s;
        const double f2 = f * f;
        const double f3 = f2 * f;

        out_pos->xyz[k] = (1.0 / 6.0) * (
            (v0 + 4.0 * v1 + v2) +
            (-3.0 * v0 + 3.0 * v2) * f +
            (3.0 * v0 - 6.0 * v1 + 3.0 * v2) * f2 +
            (-v0 + 3.0 * v1 - 3.0 * v2 + v3) * f3
        );

        out_vel[k] = (1.0 / 6.0) * (
            (-3.0 * v0 + 3.0 * v2) +
            2.0 * (3.0 * v0 - 6.0 * v1 + 3.0 * v2) * f +
            3.0 * (-v0 + 3.0 * v1 - 3.0 * v2 + v3) * f2
        ) * PATH_UPDATE_HZ;
    }

    if (is_llh)
        ecef_to_lla(out_pos->xyz, out_pos);
}
/* ---------------------------  End  ------------------------------*/

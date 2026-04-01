#ifndef PATH_H
#define PATH_H

#include "coord.h"

typedef struct {
    int n;
    coord_t *p;
    int has_prev_anchor;
    int has_next_anchor;
    double prev_anchor_xyz[3];
    double next_anchor_xyz[3];
} path_t;

#define PATH_UPDATE_HZ 100.0

int load_path_xyz(const char *file, path_t *path);
int load_path_llh(const char *file, path_t *path);
int load_path_nmea(const char *file, path_t *path);
void free_path(path_t *path);
void interpolate_path(const path_t *path, double t, coord_t *out);
void interpolate_path_llh(const path_t *path, double t, double llh[3]);
void path_clear_anchors(path_t *path);
void path_set_prev_anchor_xyz(path_t *path, const double xyz[3]);
void path_set_next_anchor_xyz(path_t *path, const double xyz[3]);
void interpolate_path_kinematic(const path_t *path, double t,
                                coord_t *out_pos, double out_vel[3],
                                int is_llh);

#endif /* PATH_H */

#ifndef RF_MODE_UTILS_H
#define RF_MODE_UTILS_H

#include <cstdint>

double clamp_double(double v, double lo, double hi);
int clamp_int(int v, int lo, int hi);

double mode_default_fs_hz(uint8_t signal_mode);
double mode_tx_center_hz(uint8_t signal_mode);
double snap_fs_to_mode_grid_hz(double fs_hz, uint8_t signal_mode);

#endif
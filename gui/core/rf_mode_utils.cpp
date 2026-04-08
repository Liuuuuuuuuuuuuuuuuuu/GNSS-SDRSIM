#include "gui/core/rf_mode_utils.h"

extern "C" {
#include "bdssim.h"
}

#include <cmath>

namespace {

double mode_min_fs_hz(uint8_t signal_mode) {
  if (signal_mode == SIG_MODE_GPS)
    return RF_GPS_ONLY_MIN_FS_HZ;
  if (signal_mode == SIG_MODE_MIXED)
    return RF_MIXED_MIN_FS_HZ;
  return RF_BDS_ONLY_MIN_FS_HZ;
}

} // namespace

double clamp_double(double v, double lo, double hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

int clamp_int(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

double mode_default_fs_hz(uint8_t signal_mode) {
  if (signal_mode == SIG_MODE_GPS)
    return RF_GPS_ONLY_MIN_FS_HZ;
  if (signal_mode == SIG_MODE_MIXED)
    return RF_MIXED_MIN_FS_HZ;
  return RF_BDS_ONLY_MIN_FS_HZ;
}

double mode_tx_center_hz(uint8_t signal_mode) {
  if (signal_mode == SIG_MODE_GPS)
    return RF_GPS_ONLY_CENTER_HZ;
  if (signal_mode == SIG_MODE_MIXED)
    return RF_MIXED_CENTER_HZ;
  return RF_BDS_ONLY_CENTER_HZ;
}

double snap_fs_to_mode_grid_hz(double fs_hz, uint8_t signal_mode) {
  const double step = RF_FS_STEP_HZ;
  const double min_fs = mode_min_fs_hz(signal_mode);
  if (fs_hz < min_fs)
    fs_hz = min_fs;

  double k = std::round(fs_hz / step);
  if (k < 1.0)
    k = 1.0;
  fs_hz = k * step;

  if (fs_hz < min_fs)
    fs_hz = min_fs;
  return fs_hz;
}

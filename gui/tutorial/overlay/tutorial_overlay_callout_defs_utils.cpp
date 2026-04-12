#include "gui/tutorial/overlay/tutorial_overlay_callout_defs_utils.h"

#include <cmath>

namespace {

constexpr uint8_t kSignalModeBds = 0;
constexpr uint8_t kSignalModeGps = 1;
constexpr uint8_t kSignalModeMixed = 2;

void add_callout(std::vector<TutorialGalaxyCalloutDef> *callouts,
                 const char *id,
                 const QString &text,
                 double angle_deg,
                 double radius_px,
                 const QSize &box_size) {
  callouts->push_back({id, text, angle_deg, radius_px, box_size});
}

} // namespace

std::vector<TutorialGalaxyCalloutDef>
tutorial_overlay_build_step_callouts(const TutorialOverlayInput &in, int step) {
  std::vector<TutorialGalaxyCalloutDef> callouts;

  auto tr_key = [&](const char *key) {
    return gui_i18n_text(in.language, key);
  };

  if (step == 1) {
    add_callout(&callouts, "search_box", tr_key("tutorial.callout.step1.search_box"),
                225.0, 338.0, QSize(220, 92));
    add_callout(&callouts, "nfz_btn", tr_key("tutorial.callout.step1.nfz_btn"),
                248.0, 314.0, QSize(220, 92));
    add_callout(&callouts, "lang_btn", tr_key("tutorial.callout.step1.lang_btn"),
                272.0, 304.0, QSize(220, 92));
    add_callout(&callouts, "dark_mode_btn", tr_key("tutorial.callout.step1.dark_mode_btn"),
                0.0, 312.0, QSize(240, 92));
    add_callout(&callouts, "guide_btn", tr_key("tutorial.callout.step1.guide_btn"),
                24.0, 314.0, QSize(220, 92));
    add_callout(&callouts, "osm_llh", tr_key("tutorial.callout.step2.osm_llh"),
                132.0, 330.0, QSize(280, 92));
  } else if (step == 2) {
    add_callout(&callouts, "smart_path", tr_key("tutorial.callout.step2.smart_path"),
                240.0, 320.0, QSize(400, 140));
    add_callout(&callouts, "straight_path", tr_key("tutorial.callout.step2.straight_path"),
                120.0, 320.0, QSize(400, 140));
    add_callout(&callouts, "mouse_hint", tr_key("tutorial.callout.step2.mouse_hint"),
                0.0, 340.0, QSize(440, 168));
  } else if (step == 3) {
    const bool show_satellite_callouts =
        in.has_navigation_data && !in.sat_points.empty();
    const bool show_g_by_mode = (in.signal_mode == kSignalModeGps ||
                                 in.signal_mode == kSignalModeMixed);
    const bool show_c_by_mode = (in.signal_mode == kSignalModeBds ||
                                 in.signal_mode == kSignalModeMixed);

    if (show_satellite_callouts && show_g_by_mode) {
      add_callout(&callouts, "sky_g", tr_key("tutorial.callout.step3.sky_g"),
                  328.0, 240.0, QSize(220, 92));
    }
    if (show_satellite_callouts && show_c_by_mode) {
      add_callout(&callouts, "sky_c", tr_key("tutorial.callout.step3.sky_c"),
                  22.0, 245.0, QSize(220, 92));
    }
  } else if (step == 4) {
    add_callout(&callouts, "wave_1", tr_key("tutorial.callout.step4.wave_1"),
                -130.0, 245.0, QSize(220, 92));
    add_callout(&callouts, "wave_2", tr_key("tutorial.callout.step4.wave_2"),
                -45.0, 245.0, QSize(220, 92));
    add_callout(&callouts, "wave_3", tr_key("tutorial.callout.step4.wave_3"),
                45.0, 245.0, QSize(220, 92));
    add_callout(&callouts, "wave_4", tr_key("tutorial.callout.step4.wave_4"),
                130.0, 245.0, QSize(220, 92));
  } else if (step == 5) {
    add_callout(&callouts, "sig_bdt_gpst", tr_key("tutorial.callout.step5.sig_bdt_gpst"),
                332.0, 248.0, QSize(260, 104));
    add_callout(&callouts, "sig_rnx", tr_key("tutorial.callout.step5.sig_rnx"),
                12.0, 248.0, QSize(260, 104));
    add_callout(&callouts, "sig_tab_simple", tr_key("tutorial.callout.step5.sig_tab_simple"),
                92.0, 240.0, QSize(240, 104));
    add_callout(&callouts, "sig_tab_detail", tr_key("tutorial.callout.step5.sig_tab_detail"),
                132.0, 240.0, QSize(240, 104));
  } else if (step == 6) {
    add_callout(&callouts, "sig_interfere", tr_key("tutorial.callout.step6.sig_interfere"),
                350.0, 250.0, QSize(240, 104));
    add_callout(&callouts, "sig_system", tr_key("tutorial.callout.step6.sig_system"),
                26.0, 262.0, QSize(240, 104));
    add_callout(&callouts, "sig_fs", tr_key("tutorial.callout.step6.sig_fs"),
                300.0, 270.0, QSize(240, 104));
    add_callout(&callouts, "sig_tx", tr_key("tutorial.callout.step6.sig_tx"),
                338.0, 292.0, QSize(256, 104));
    add_callout(&callouts, "sig_start", tr_key("tutorial.callout.step6.sig_start"),
                40.0, 308.0, QSize(240, 104));
    add_callout(&callouts, "sig_exit", tr_key("tutorial.callout.step6.sig_exit"),
                12.0, 308.0, QSize(240, 104));
  } else if (step == 7) {
    add_callout(&callouts, "detail_sats", tr_key("tutorial.callout.step7.detail_sats"),
                22.0, 240.0, QSize(220, 104));
    add_callout(&callouts, "gain_slider", tr_key("tutorial.callout.step7.gain_slider"),
                180.0, 280.0, QSize(220, 104));
    add_callout(&callouts, "cn0_slider", tr_key("tutorial.callout.step7.cn0_slider"),
                240.0, 300.0, QSize(220, 104));
    add_callout(&callouts, "path_v_slider", tr_key("tutorial.callout.step7.path_v_slider"),
                338.0, 250.0, QSize(230, 104));
    add_callout(&callouts, "path_a_slider", tr_key("tutorial.callout.step7.path_a_slider"),
                338.0, 300.0, QSize(230, 104));
    add_callout(&callouts, "prn_slider", tr_key("tutorial.callout.step7.prn_slider"),
                338.0, 340.0, QSize(230, 104));
    add_callout(&callouts, "ch_slider", tr_key("tutorial.callout.step7.ch_slider"),
                350.0, 360.0, QSize(220, 104));
  } else if (step == 8) {
    add_callout(&callouts, "sw_fmt", tr_key("tutorial.callout.step8.sw_fmt"),
                190.0, 300.0, QSize(220, 104));
    add_callout(&callouts, "sw_mode", tr_key("tutorial.callout.step8.sw_mode"),
                338.0, 320.0, QSize(220, 104));
    add_callout(&callouts, "tg_meo", tr_key("tutorial.callout.step8.tg_meo"),
                45.0, 360.0, QSize(220, 104));
    add_callout(&callouts, "tg_iono", tr_key("tutorial.callout.step8.tg_iono"),
                180.0, 360.0, QSize(220, 104));
    add_callout(&callouts, "tg_clk", tr_key("tutorial.callout.step8.tg_clk"),
                315.0, 360.0, QSize(220, 104));
  }

  return callouts;
}

void tutorial_overlay_scale_guide_callouts(
    std::vector<TutorialGalaxyCalloutDef> *callouts) {
  if (!callouts) {
    return;
  }

  const double kGuideCalloutScaleX = 1.10;
  const double kGuideCalloutScaleY = 1.14;
  for (TutorialGalaxyCalloutDef &callout : *callouts) {
    callout.box_size.setWidth(
        (int)std::lround(callout.box_size.width() * kGuideCalloutScaleX));
    callout.box_size.setHeight(
        (int)std::lround(callout.box_size.height() * kGuideCalloutScaleY));
  }
}

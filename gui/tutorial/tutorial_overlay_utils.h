#ifndef TUTORIAL_OVERLAY_UTILS_H
#define TUTORIAL_OVERLAY_UTILS_H

#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QSize>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "gui/core/gui_i18n.h"
#include "gui/core/signal_snapshot.h"

class QWidget;

struct TutorialOverlayInput {
  QWidget *host_widget = nullptr;
  int win_width = 0;
  int win_height = 0;
  GuiLanguage language = GuiLanguage::English;
  bool overlay_visible = false;
  bool running_ui = false;
  bool detailed = false;
  bool has_navigation_data = false;
  int sat_mode = 2; // 0=single-candidate, 1=by-system PRN ranges, 2=show all
  int active_prn_mask_len = 0;
  std::vector<int> active_prn_mask;
  int single_candidate_count = 0;
  std::vector<int> single_candidates;
  uint8_t signal_mode = 0; // 0=BDS, 1=GPS, 2=BDS+GPS
  std::vector<SatPoint> sat_points;
  int step = 0;
  QRect osm_panel_rect;
  QRect osm_stop_btn_rect;
  QRect osm_runtime_rect;
  QRect search_box_rect;
  QRect nfz_btn_rect;
  QRect dark_mode_btn_rect;
  QRect tutorial_toggle_rect;
  QRect lang_btn_rect;
  std::vector<QRect> osm_status_badge_rects;
  std::vector<QRect> osm_nfz_legend_row_rects;
  QRect signal_clean_rect;
  QPixmap signal_clean_snapshot;
  QRect waveform_clean_rect;
  QPixmap waveform_clean_snapshot;
  QRect osm_lower_clean_rect;
  QPixmap osm_lower_clean_snapshot;
  int last_step = 0;
};

struct TutorialOverlayState {
  QRect prev_btn_rect;
  QRect next_btn_rect;
  QRect close_btn_rect;
  QRect contents_btn_rect;            // back-to-contents button (steps 1-7 only)
  int anim_step_anchor = -1;
  std::chrono::steady_clock::time_point anim_start_tp;
  int spotlight_index = 0;
  int text_page = 0;
  int text_page_count = 1;
  int text_page_anchor_step = -1;
  int text_page_anchor_spotlight = -1;
  // TOC jump buttons (valid when step == 0)
  QRect toc_btn_rects[5];
  int toc_btn_targets[5] = {1, 3, 4, 5, 7};
  // Callout hit-test data — rebuilt every frame, used for click detection
  std::vector<QRectF> callout_hit_boxes;
  std::vector<QPointF> callout_hit_anchors;
  // Element glow animation (triggered on callout click)
  bool has_glow = false;
  QPointF glow_anchor;
  int glow_step = -1;
  std::chrono::steady_clock::time_point glow_start_tp;
};

enum class TutorialGalaxySceneId {
  Map = 0,
  SubSatellitePoint = 1,
  SignalSetting = 2,
  Waveform = 3,
};

struct TutorialGalaxyCalloutDef {
  QString id;
  QString text;
  double angle_deg = 0.0;
  double radius_px = 220.0;
  QSize box_size = QSize(220, 96);
};

struct TutorialGalaxySceneDef {
  TutorialGalaxySceneId scene_id = TutorialGalaxySceneId::Map;
  QWidget *target_widget = nullptr;
  QRect target_local_rect;
  double center_scale = 1.08;
  int mask_alpha = 192;
  double move_duration_sec = 0.72;
  std::vector<TutorialGalaxyCalloutDef> callouts;
};

struct TutorialGalaxyNodeLayout {
  TutorialGalaxyCalloutDef def;
  QPointF center;
  QPointF edge_anchor;
  QRectF box_rect;
};

struct TutorialGalaxyRenderFrame {
  QRectF snapshot_rect;
  QRectF source_rect;
  double progress = 0.0;
  std::vector<TutorialGalaxyNodeLayout> nodes;
};

std::vector<QPointF>
calculateRadialPositions(const QPointF &center,
                        const std::vector<TutorialGalaxyCalloutDef> &callouts,
                        double radius_scale = 1.0);

TutorialGalaxySceneDef tutorial_build_default_galaxy_scene(
    TutorialGalaxySceneId scene_id, QWidget *target_widget,
    GuiLanguage language);

class TutorialGalaxyOverlayController {
 public:
  void begin(const TutorialGalaxySceneDef &scene, QWidget *overlay_widget);
  void stop();

  bool visible() const;
  bool handleMousePress(const QPoint &pos) const;
  bool handleKeyPress(int key) const;

  void tick();
  TutorialGalaxyRenderFrame buildFrame(const QRect &viewport_rect) const;
  void paint(QPainter &p, const QRect &viewport_rect) const;

 private:
  TutorialGalaxySceneDef scene_;
  QWidget *overlay_widget_ = nullptr;
  QPixmap snapshot_;
  QRect source_rect_;
  QRectF start_rect_;
  QRectF target_rect_;
  std::chrono::steady_clock::time_point start_tp_;
  bool running_ = false;
};

QRect tutorial_focus_rect_for_step(const TutorialOverlayInput &in);
void tutorial_draw_overlay(QPainter &p, const TutorialOverlayInput &in,
                           TutorialOverlayState *state);

// Immediate-mode (ImGui/custom renderer) guide model.
// This section is framework-agnostic and can be bound to ImGui draw-list calls.

enum class TutorialImSceneId {
  Map = 0,
  Skyplot = 1,
  SignalSetting = 2,
  Waveform = 3,
};

struct TutorialImVec2 {
  float x = 0.0f;
  float y = 0.0f;
};

struct TutorialImRect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

struct TutorialImCalloutDef {
  std::string id;
  std::string text;
  float angle_deg = 0.0f;
  float radius_px = 220.0f;
  float box_w = 240.0f;
  float box_h = 92.0f;
};

struct TutorialSunAnchorPoint {
  std::string component_id;
  TutorialImVec2 abs_pos;
  bool valid = false;
};

struct TutorialGalaxyGuideStepConfig {
  std::string anchor_component_id;
  std::string text;
  float angle_deg = 0.0f;
  float radius_px = 300.0f;
};

struct SunWindowData {
  TutorialImRect rect;
  std::vector<TutorialSunAnchorPoint> anchor_buffer;
  std::vector<TutorialGalaxyGuideStepConfig> guide_steps;
  float default_radius_px = 300.0f;
  float orbit_spin_deg = 0.0f;
};

struct TutorialImSceneDef {
  TutorialImSceneId scene_id = TutorialImSceneId::Map;
  TutorialImRect source_rect;
  float center_scale = 1.08f;
  float move_duration_sec = 0.70f;
  int mask_alpha = 200;
  std::vector<TutorialImCalloutDef> callouts;
};

struct TutorialImSnapshot {
  int fbo_handle = 0;
  std::uint64_t texture_id = 0;
  int tex_w = 0;
  int tex_h = 0;
  bool valid = false;
};

struct TutorialImRuntime {
  bool active = false;
  TutorialImSceneDef scene;
  TutorialImSnapshot snapshot;
  std::string background_window_name = "##tutorial_bg";
  std::string center_window_name = "##tutorial_center";
  int step_index = 0;
  int focused_step_index = -1;
  bool focus_center_window_once = false;
  std::chrono::steady_clock::time_point anim_start_tp;
};

struct TutorialImCalloutLayout {
  TutorialImCalloutDef def;
  TutorialImVec2 center;
  TutorialImVec2 edge_anchor;
  TutorialImRect box;
};

struct TutorialImFrameLayout {
  TutorialImRect current_rect;
  float progress = 0.0f;
  std::vector<TutorialImCalloutLayout> callouts;
};

TutorialImSceneDef tutorial_im_build_scene(TutorialImSceneId scene_id);

TutorialImSnapshot tutorial_im_capture_to_fbo_pseudocode(
    const TutorialImRect &src_rect);

void tutorial_im_on_step_changed(TutorialImRuntime *rt, int new_step,
                 std::chrono::steady_clock::time_point now_tp);

TutorialImFrameLayout tutorial_im_build_frame_layout(
    const TutorialImRuntime &rt, float screen_w, float screen_h,
    std::chrono::steady_clock::time_point now_tp);

std::vector<TutorialImVec2> tutorial_im_calculate_radial_positions(
    const TutorialImVec2 &center,
    const std::vector<TutorialImCalloutDef> &callouts,
    float radius_scale = 1.0f);

void RenderRadialCallouts(TutorialImFrameLayout *layout,
                          const TutorialImRect &center_rect);

void DrawGalaxyCallouts(const SunWindowData &central_window);

void tutorial_im_render_overlay_pseudocode(TutorialImRuntime *rt,
                                           float screen_w,
                                           float screen_h);

#endif
void MapWidget::draw_tutorial_overlay(QPainter &p, int win_width,
                                      int win_height) {
  bool running_ui = false;
  bool detailed = false;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    running_ui = g_ctrl.running_ui;
    detailed = g_ctrl.show_detailed_ctrl;
  }

  TutorialOverlayInput overlay_in;
  overlay_in.win_width = win_width;
  overlay_in.win_height = win_height;
  overlay_in.language = ui_language_;
  overlay_in.overlay_visible = tutorial_overlay_visible_;
  overlay_in.running_ui = running_ui;
  overlay_in.detailed = detailed;
  overlay_in.step = tutorial_step_;
  overlay_in.osm_panel_rect = osm_panel_rect_;
  overlay_in.osm_stop_btn_rect = osm_stop_btn_rect_;
  overlay_in.last_step = tutorial_last_step();

  TutorialOverlayState overlay_state;
  overlay_state.prev_btn_rect = tutorial_prev_btn_rect_;
  overlay_state.next_btn_rect = tutorial_next_btn_rect_;
  overlay_state.close_btn_rect = tutorial_close_btn_rect_;
  overlay_state.anim_step_anchor = tutorial_anim_step_anchor_;
  overlay_state.anim_start_tp = tutorial_anim_start_tp_;
  overlay_state.spotlight_index = tutorial_spotlight_index_;
  overlay_state.text_page = tutorial_text_page_;
  overlay_state.text_page_count = tutorial_text_page_count_;
  overlay_state.text_page_anchor_step = tutorial_text_page_anchor_step_;
  overlay_state.text_page_anchor_spotlight =
      tutorial_text_page_anchor_spotlight_;

  tutorial_draw_overlay(p, overlay_in, &overlay_state);

  tutorial_prev_btn_rect_ = overlay_state.prev_btn_rect;
  tutorial_next_btn_rect_ = overlay_state.next_btn_rect;
  tutorial_close_btn_rect_ = overlay_state.close_btn_rect;
  tutorial_anim_step_anchor_ = overlay_state.anim_step_anchor;
  tutorial_anim_start_tp_ = overlay_state.anim_start_tp;
  tutorial_spotlight_index_ = overlay_state.spotlight_index;
  tutorial_text_page_ = overlay_state.text_page;
  tutorial_text_page_count_ = overlay_state.text_page_count;
  tutorial_text_page_anchor_step_ = overlay_state.text_page_anchor_step;
  tutorial_text_page_anchor_spotlight_ =
      overlay_state.text_page_anchor_spotlight;
}

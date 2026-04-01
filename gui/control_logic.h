#ifndef CONTROL_LOGIC_H
#define CONTROL_LOGIC_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "gui/control_layout.h"
#include "gui/control_state.h"

bool control_logic_handle_click(int x,
                                int y,
                                int win_width,
                                int win_height,
                                GuiControlState *ctrl,
                                std::mutex *ctrl_mtx,
                                std::atomic<uint32_t> *start_req,
                                std::atomic<uint32_t> *stop_req,
                                std::atomic<uint32_t> *exit_req,
                                volatile int *runtime_abort);

bool control_logic_value_text_for_field(int field_id,
                                        char *out,
                                        size_t out_sz,
                                        GuiControlState *ctrl,
                                        std::mutex *ctrl_mtx);

bool control_logic_handle_value_input(int field_id,
                                      const char *input,
                                      GuiControlState *ctrl,
                                      std::mutex *ctrl_mtx);

bool control_logic_handle_slider_drag(int slider_id,
                                      int x,
                                      int win_width,
                                      int win_height,
                                      GuiControlState *ctrl,
                                      std::mutex *ctrl_mtx);

#endif

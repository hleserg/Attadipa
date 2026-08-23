#pragma once

#include "attadipa/core/input.h"

struct _lv_indev_t;

namespace attadipa::sim {

// Binds the input layer to LVGL.
//
// The queue is the input layer; this file is the one place that knows LVGL
// exists. Two sources push into that queue and neither can tell the difference
// at the far end:
//
//   * a person, through LVGL's own SDL mouse and keyboard devices, which stay
//     registered and keep working -- section 2 of the request is explicit that
//     physical input must not stop when remote input starts;
//   * the debug bridge.
//
// ### Why the pointer is an LVGL input device and the buttons are not
//
// A touch is exactly what `lv_indev_data_t` models: one point and a pressed
// flag. Registering a second pointer device alongside the SDL mouse is the
// supported way to have two hands on one screen, and LVGL arbitrates.
//
// A button is not. LVGL has `LV_INDEV_TYPE_KEYPAD`, which needs a *key* --
// LV_KEY_ENTER, LV_KEY_NEXT -- and this project has not decided what its
// buttons mean. On the Waveshare, which named input each of the two pressable
// buttons even reaches is open question D5. Inventing a mapping here to make
// the plumbing look complete would put a guess about hardware in the graphics
// layer, which is the failure CLAUDE.md's first rule exists to prevent. So
// button events are delivered to a listener instead, and the diagnostic screen
// shows them: the event provably arrives, and nothing claims to know what it
// should do.

void remote_input_attach(core::InputQueue& queue);

// Drains the queue into LVGL. Called once per frame, before lv_timer_handler.
void remote_input_pump();

// Notified for every event drained, whatever its origin.
//
// A listener rather than a return value because the diagnostic screen has to
// see what the *interface* saw, not what a caller intended -- if a swipe were
// drawn from the host's request instead of from the queue, the picture would
// prove the host's arithmetic and nothing about the device.
using InputListener = void (*)(const core::InputEvent& event);
void remote_input_set_button_listener(InputListener listener);
void remote_input_set_pointer_listener(InputListener listener);

}  // namespace attadipa::sim

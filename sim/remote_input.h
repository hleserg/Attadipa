#pragma once

#include "attadipa/core/input.h"

#include <cstddef>

struct _lv_indev_t;

namespace attadipa::sim {

// Binds the input layer to LVGL.
//
// The queue is the input layer; this file is the one place that knows LVGL
// exists. It drains the queue into a **second** LVGL pointer device, which
// coexists with the SDL mouse a person uses:
//
//   * a person's mouse and keyboard reach LVGL through LVGL's own SDL devices,
//     which stay registered and keep working -- section 2 of the request is
//     explicit that physical input must not stop when remote input starts.
//     They do **not** pass through `core::InputQueue`;
//   * the debug bridge pushes into the queue, and this file carries it from
//     there into LVGL.
//
// They coexist because LVGL runs each registered indev on its own read timer
// with its own press state, not because they meet in the queue. An earlier
// version of this comment said "two sources push into that queue", which was
// never true of the simulator; the queue's only producer today is the bridge,
// and `InputOrigin::Physical` is waiting for T-114's touch controller. Said
// plainly because the disconnect-safety story rests on that enum, and a
// mechanism nothing exercises is a mechanism nobody has tested.
//
// ### Why the pointer is an LVGL input device and the buttons are not
//
// A touch is exactly what `lv_indev_data_t` models: one point and a pressed
// flag. Registering a second pointer device alongside the SDL mouse is the
// supported way to have two hands on one screen. LVGL does not *arbitrate*
// between them -- each indev dispatches independently to whatever lies under
// its own point -- so the two do not see each other, and a release from one
// clears the `LV_STATE_PRESSED` the other is holding on the same widget. That
// is fine here (a person and a test driving the same screen at the same
// instant is not a case worth designing for) and it is written down because
// "LVGL arbitrates" was the wrong word for it.
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

// How many pointer transitions are queued for LVGL's read timer and not yet
// consumed. Zero means the FIFO is empty, which is one of the three things
// "the interface has settled" has to mean -- the other two are LVGL's own idle
// timer and its animation count, and neither can see this FIFO. Exposed rather
// than inferred because `wait_stable` answered `ok` with a tap still sitting
// here, and the screenshot after it caught the frame from before the tap.
std::size_t remote_input_pending();

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

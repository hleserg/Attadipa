#include "remote_input.h"

#include "lvgl.h"

namespace attadipa::sim {
namespace {

core::InputQueue* g_queue = nullptr;
InputListener     g_button_listener  = nullptr;
InputListener     g_pointer_listener = nullptr;

// What LVGL should see on its next read. LVGL polls; the queue pushes. The gap
// between the two is this pair of variables, and it exists because a tap whose
// down and up both landed between two polls would otherwise never be seen at
// all -- the press would be set and cleared before anything looked.
bool          g_pointer_pressed = false;
std::int16_t  g_pointer_x       = 0;
std::int16_t  g_pointer_y       = 0;

// Set when a press arrived and was not yet reported. Held for one read so that
// a zero-duration tap still produces a press LVGL can see.
bool g_press_pending  = false;
bool g_release_pending = false;

void read_pointer(lv_indev_t* indev, lv_indev_data_t* data)
{
    (void)indev;
    data->point.x = g_pointer_x;
    data->point.y = g_pointer_y;

    if (g_press_pending) {
        data->state      = LV_INDEV_STATE_PRESSED;
        g_press_pending  = false;
        // If the release arrived in the same batch, it waits for the next read
        // rather than being dropped. LVGL needs to observe the pressed state at
        // least once for a click to exist.
        return;
    }
    if (g_release_pending) {
        data->state       = LV_INDEV_STATE_RELEASED;
        g_release_pending = false;
        g_pointer_pressed = false;
        return;
    }
    data->state = g_pointer_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

void remote_input_attach(core::InputQueue& queue)
{
    g_queue = &queue;

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_pointer);
}

void remote_input_set_button_listener(InputListener listener)
{
    g_button_listener = listener;
}

void remote_input_set_pointer_listener(InputListener listener)
{
    g_pointer_listener = listener;
}

void remote_input_pump()
{
    if (g_queue == nullptr) {
        return;
    }

    core::InputEvent event;
    while (g_queue->pop(event)) {
        switch (event.type) {
        case core::InputEventType::PointerDown:
            g_pointer_x       = event.x;
            g_pointer_y       = event.y;
            g_pointer_pressed = true;
            g_press_pending   = true;
            if (g_pointer_listener != nullptr) {
                g_pointer_listener(event);
            }
            break;
        case core::InputEventType::PointerMove:
            g_pointer_x = event.x;
            g_pointer_y = event.y;
            if (g_pointer_listener != nullptr) {
                g_pointer_listener(event);
            }
            break;
        case core::InputEventType::PointerUp:
            g_pointer_x       = event.x;
            g_pointer_y       = event.y;
            g_release_pending = true;
            if (g_pointer_listener != nullptr) {
                g_pointer_listener(event);
            }
            break;
        case core::InputEventType::ButtonDown:
        case core::InputEventType::ButtonUp:
            if (g_button_listener != nullptr) {
                g_button_listener(event);
            }
            break;
        }

        // **One pointer event per pump, and this is the load-bearing line.**
        //
        // LVGL reads its input devices once per `lv_timer_handler`. Draining a
        // whole swipe in one go would leave only the last coordinate for LVGL
        // to see, collapsing thirty points into a single jump from the first to
        // the last -- which is exactly the artificial high-level swipe the
        // request says not to produce, arriving through the back door of an
        // over-eager drain. Buttons are not polled, so they cost nothing to
        // drain in a batch and do not stop it.
        const bool is_pointer = event.type == core::InputEventType::PointerDown ||
                                event.type == core::InputEventType::PointerMove ||
                                event.type == core::InputEventType::PointerUp;
        if (is_pointer) {
            break;
        }
    }
}

}  // namespace attadipa::sim

#include "attadipa/core/input.h"

namespace attadipa::core {

bool InputQueue::push(const InputEvent& event)
{
    if (count_ == kCapacity) {
        ++stats_.dropped;
        return false;
    }
    buffer_[(head_ + count_) % kCapacity] = event;
    ++count_;
    ++stats_.pushed;
    return true;
}

bool InputQueue::pop(InputEvent& out)
{
    if (count_ == 0) {
        return false;
    }
    out   = buffer_[head_];
    head_ = (head_ + 1) % kCapacity;
    --count_;
    ++stats_.popped;
    return true;
}

void InputQueue::clear()
{
    head_  = 0;
    count_ = 0;
}

bool InputState::button_down(std::uint8_t index) const
{
    return index < kMaxButtons && button_held_[index];
}

InputOrigin InputState::button_origin(std::uint8_t index) const
{
    return index < kMaxButtons ? button_origin_[index] : InputOrigin::Physical;
}

bool InputState::apply(const InputEvent& event, std::uint8_t button_count)
{
    switch (event.type) {
    case InputEventType::ButtonDown:
        // Past the profile's list, or past the array. Both are the caller
        // asking for a key this board does not have, which is an error rather
        // than something to clamp: a test that silently pressed button 0 when
        // it meant button 5 would pass while testing nothing.
        if (event.button >= button_count || event.button >= kMaxButtons) {
            return false;
        }
        // Already held. Not an error to be tolerated quietly either: a
        // repeated down without an up is how a stuck button reaches the
        // interface, and the long-press timer above would restart.
        if (button_held_[event.button]) {
            return false;
        }
        button_held_[event.button]   = true;
        button_origin_[event.button] = event.origin;
        return true;

    case InputEventType::ButtonUp:
        if (event.button >= button_count || event.button >= kMaxButtons) {
            return false;
        }
        if (!button_held_[event.button]) {
            return false;
        }
        button_held_[event.button] = false;
        return true;

    case InputEventType::PointerDown:
        // The single-point limit, enforced rather than assumed. A second
        // finger is refused with an error the caller can report; it is never
        // merged into the first, which would look like an impossibly fast
        // drag across the screen.
        if (event.touch_id >= kMaxTouchPoints) {
            return false;
        }
        if (pointer_down_) {
            return false;
        }
        pointer_down_     = true;
        pointer_touch_id_ = event.touch_id;
        pointer_x_        = event.x;
        pointer_y_        = event.y;
        pointer_origin_   = event.origin;
        return true;

    case InputEventType::PointerMove:
        // A move with nothing down is a hover. Neither panel reports one --
        // both touch controllers are contact devices -- so it is refused here
        // rather than becoming a phantom drag from wherever the last press
        // happened to end.
        if (!pointer_down_ || event.touch_id != pointer_touch_id_) {
            return false;
        }
        pointer_x_ = event.x;
        pointer_y_ = event.y;
        return true;

    case InputEventType::PointerUp:
        if (!pointer_down_ || event.touch_id != pointer_touch_id_) {
            return false;
        }
        pointer_down_ = false;
        pointer_x_    = event.x;
        pointer_y_    = event.y;
        return true;
    }
    return false;
}

std::uint8_t InputState::release_all(InputOrigin origin, std::uint32_t at_ms, InputQueue& queue)
{
    std::uint8_t written = 0;

    for (std::uint8_t i = 0; i < kMaxButtons; ++i) {
        if (button_held_[i] && button_origin_[i] == origin) {
            InputEvent up;
            up.type   = InputEventType::ButtonUp;
            up.origin = origin;
            up.button = i;
            up.at_ms  = at_ms;
            // Cleared only if the interface was actually told. A release that
            // could not be queued must stay held, because the widget under it
            // still thinks it is pressed -- and something has to retry. The
            // debug bridge's hold expiry does, which is why the worst case is
            // a stuck press for max_hold_ms rather than forever.
            if (queue.push(up)) {
                button_held_[i] = false;
                ++written;
            }
        }
    }

    // The pointer is lifted where it was last seen rather than at (0, 0).
    // A release at the origin of the screen would land on whatever widget sits
    // in the corner and can fire it -- the cleanup would itself be an input
    // event with consequences, which is the opposite of cleaning up.
    if (pointer_down_ && pointer_origin_ == origin) {
        InputEvent up;
        up.type     = InputEventType::PointerUp;
        up.origin   = origin;
        up.x        = pointer_x_;
        up.y        = pointer_y_;
        up.touch_id = pointer_touch_id_;
        up.at_ms    = at_ms;
        if (queue.push(up)) {
            pointer_down_ = false;
            ++written;
        }
    }

    return written;
}

}  // namespace attadipa::core

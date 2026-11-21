/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_input_processor_pinch_zoom

#include <drivers/input_processor.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zmk/events/keycode_state_changed.h>

static int pinch_zoom(const struct device *dev, struct input_event *event,
                      uint32_t zoom_out, uint32_t zoom_in,
                      struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_REL || event->code != INPUT_REL_MISC) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* The controller gates successive zoom steps by zoom-consecutive-distance. */
    if (event->value != 0) {
        uint32_t keycode = event->value > 0 ? zoom_in : zoom_out;
        int64_t now = k_uptime_get();
        raise_zmk_keycode_state_changed_from_encoded(keycode, true, now);
        raise_zmk_keycode_state_changed_from_encoded(keycode, false, now);
    }
    return ZMK_INPUT_PROC_STOP;
}

static const struct zmk_input_processor_driver_api api = {.handle_event = pinch_zoom};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

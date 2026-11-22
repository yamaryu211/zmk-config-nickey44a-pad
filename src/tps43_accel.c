/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_input_processor_tps43_accel

#include <drivers/input_processor.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <string.h>

struct axis_history {
    int64_t last_ms;
    int32_t remainder;
    bool has_sample;
};

struct accel_data {
    struct axis_history axes[2];
};

/* Gain in thousandths. Interpolate quadratically from 0.8x to 2.2x. */
static int32_t gain_for_motion(int32_t delta, int64_t elapsed_ms) {
    uint64_t distance = delta < 0 ? -(int64_t)delta : delta;
    int64_t speed = distance * 1000 / MAX(elapsed_ms, 1);
    int32_t progress = CLAMP(speed - 500, 0, 2500);
    return 800 + (int64_t)1400 * progress * progress / (2500 * 2500);
}

static int accelerate(const struct device *dev, struct input_event *event, uint32_t param1,
                      uint32_t param2, struct zmk_input_processor_state *state) {
    struct accel_data *data = dev->data;
    if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH) {
        memset(data, 0, sizeof(*data));
    }
    if (event->type != INPUT_EV_REL || (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* X and Y arrive separately, even when they belong to a single sensor report. */
    struct axis_history *axis = &data->axes[event->code == INPUT_REL_Y];
    int64_t now = k_uptime_get();
    int32_t gain = axis->has_sample ? gain_for_motion(event->value, now - axis->last_ms) : 800;
    axis->has_sample = true;
    axis->last_ms = now;
    int64_t scaled = (int64_t)event->value * gain + axis->remainder;
    axis->remainder = scaled % 1000;
    event->value = CLAMP(scaled / 1000, INT16_MIN, INT16_MAX);
    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api api = {.handle_event = accelerate};

#define ACCEL_INSTANCE(n)                                                                         \
    static struct accel_data data_##n;                                                            \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &data_##n, NULL, POST_KERNEL,                            \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

DT_INST_FOREACH_STATUS_OKAY(ACCEL_INSTANCE)

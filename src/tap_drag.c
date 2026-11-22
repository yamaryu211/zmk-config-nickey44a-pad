/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_input_processor_tap_drag

#include <drivers/input_processor.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

enum tap_drag_phase {
    IDLE,
    AFTER_TAP,
    SECOND_CONTACT,
    DRAGGING,
    WAITING_FOR_RETOUCH,
    RETOUCHED,
    IGNORE_TRAILING_TAP,
};

struct tap_drag_data {
    enum tap_drag_phase phase;
    int64_t deadline;
#if IS_ENABLED(CONFIG_SHIELD_TPS43_EDGE)
    bool edge_scroll;
#endif
    struct k_work_delayable drop_work;
    const struct device *dev;
};

/* Only the Zephyr input thread reads/writes gesture state and sends button reports. */
static struct tap_drag_data *waiting_pad;

static void end_gesture(struct tap_drag_data *data) {
    if (waiting_pad == data) {
        waiting_pad = NULL;
        k_work_cancel_delayable(&data->drop_work);
    }
    if (data->phase == DRAGGING || data->phase == WAITING_FOR_RETOUCH || data->phase == RETOUCHED) {
        zmk_hid_mouse_button_release(0);
        zmk_endpoint_send_mouse_report();
        /* The TPS43 may send a tap button pair after reporting contact-up. */
        data->phase = IGNORE_TRAILING_TAP;
    } else if (data->phase != IGNORE_TRAILING_TAP) {
        data->phase = IDLE;
    }
}

static void notify_drop_timeout(struct k_work *work) {
    struct tap_drag_data *data = CONTAINER_OF(k_work_delayable_from_work(work),
                                              struct tap_drag_data, drop_work);
    /* Queue behind physical input. A full input queue must not strand a held button. */
    if (input_report(data->dev, INPUT_EV_MSC, 0, 0, false, K_NO_WAIT) < 0) {
        k_work_schedule(&data->drop_work, K_MSEC(1));
    }
}

static void on_drop_timeout(struct input_event *event, void *user_data) {
    struct tap_drag_data *data = event->dev->data;
    /* A queued notification can outlive a retouch and a subsequent lift. */
    if (data->phase == WAITING_FOR_RETOUCH && k_uptime_get() >= data->deadline) {
        end_gesture(data);
    }
}

static void contact_changed(struct tap_drag_data *data, bool down, uint32_t grace_ms, int64_t now) {
    if (down) {
        if (data->phase == WAITING_FOR_RETOUCH) {
            waiting_pad = NULL;
            k_work_cancel_delayable(&data->drop_work);
            data->phase = RETOUCHED;
        } else if (data->phase != DRAGGING && data->phase != RETOUCHED &&
                   data->phase != SECOND_CONTACT) {
            data->phase = data->phase == AFTER_TAP && now <= data->deadline ? SECOND_CONTACT : IDLE;
        }
    } else if (data->phase == DRAGGING) {
        data->phase = WAITING_FOR_RETOUCH;
        data->deadline = now + grace_ms;
        waiting_pad = data;
        k_work_reschedule(&data->drop_work, K_MSEC(grace_ms));
    } else if (data->phase != WAITING_FOR_RETOUCH) {
        /* Lifting a stationary retouch is a tap to drop immediately. */
        end_gesture(data);
    }
}

#if IS_ENABLED(CONFIG_SHIELD_TPS43_EDGE)
static void apply_edge_scroll(struct tap_drag_data *data, struct input_event *event) {
    if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH) {
        data->edge_scroll = false;
    } else if (event->type == INPUT_EV_ABS && event->code == INPUT_ABS_Y) {
        /* After Nickey's orientation correction, low sensor Y is the right edge.
         * An armed or resumed tap-drag always owns its second contact. */
        data->edge_scroll = data->phase == IDLE && event->value >= 0 && event->value < 60;
    } else if (data->edge_scroll && event->type == INPUT_EV_REL) {
        if (event->code == INPUT_REL_X) {
            /* The shared orientation transform maps native HWHEEL to host WHEEL. */
            event->code = INPUT_REL_HWHEEL;
        } else if (event->code == INPUT_REL_Y) {
            /* Keep the driver's end-of-frame sync without sending sideways movement. */
            event->value = 0;
        } else {
            data->edge_scroll = false;
        }
    }
}
#endif

static int tap_drag(const struct device *dev, struct input_event *event,
                    uint32_t tapping_term_ms, uint32_t grace_ms,
                    struct zmk_input_processor_state *state) {
    struct tap_drag_data *data = dev->data;
    int64_t now = k_uptime_get();

    if (waiting_pad && (waiting_pad != data || now >= waiting_pad->deadline)) {
        end_gesture(waiting_pad);
    }

#if IS_ENABLED(CONFIG_SHIELD_TPS43_EDGE)
    apply_edge_scroll(data, event);
    if (event->type == INPUT_EV_ABS && event->code == INPUT_ABS_Y) {
        return ZMK_INPUT_PROC_STOP;
    }
#endif
    if (event->type == INPUT_EV_KEY) {
        switch (event->code) {
        case INPUT_BTN_TOUCH:
            contact_changed(data, event->value != 0, grace_ms, now);
            /* Both edges must reach the inertia processor. */
            break;
        case INPUT_BTN_0:
            if (data->phase == IGNORE_TRAILING_TAP || data->phase == WAITING_FOR_RETOUCH ||
                data->phase == RETOUCHED || data->phase == DRAGGING) {
                event->code = INPUT_KEY_RESERVED;
            } else if (!event->value) {
                /* With hardware press-and-hold disabled, this is a completed tap. */
                data->phase = AFTER_TAP;
                data->deadline = now + tapping_term_ms;
            }
            break;
        case INPUT_BTN_1:
            end_gesture(data);
            break;
        }
    } else if (event->type == INPUT_EV_REL) {
        switch (event->code) {
        case INPUT_REL_X:
        case INPUT_REL_Y:
            if (event->value && data->phase == RETOUCHED) {
                data->phase = DRAGGING;
            } else if (event->value && data->phase == SECOND_CONTACT) {
                /* Press before forwarding the first motion, even if it is the Y half of a frame. */
                zmk_hid_mouse_button_press(0);
                zmk_endpoint_send_mouse_report();
                data->phase = DRAGGING;
            }
            break;
        case INPUT_REL_WHEEL:
        case INPUT_REL_HWHEEL:
        case INPUT_REL_MISC:
            end_gesture(data);
            break;
        }
    }
    return ZMK_INPUT_PROC_CONTINUE;
}

static int initialize(const struct device *dev) {
    struct tap_drag_data *data = dev->data;
    data->dev = dev;
    k_work_init_delayable(&data->drop_work, notify_drop_timeout);
    return 0;
}

static const struct zmk_input_processor_driver_api api = {.handle_event = tap_drag};

#define TAP_DRAG_INSTANCE(n)                                                                      \
    static struct tap_drag_data data_##n;                                                         \
    DEVICE_DT_INST_DEFINE(n, initialize, NULL, &data_##n, NULL, POST_KERNEL,                      \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);                             \
    INPUT_CALLBACK_DEFINE_NAMED(DEVICE_DT_INST_GET(n), on_drop_timeout, NULL, tap_drag_timeout_##n);

DT_INST_FOREACH_STATUS_OKAY(TAP_DRAG_INSTANCE)

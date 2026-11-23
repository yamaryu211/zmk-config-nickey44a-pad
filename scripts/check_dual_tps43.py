"""Check both Nickey firmware builds, gesture routing and physical key positions.

Run with the Python environment used by west. This checks configuration and
event semantics; it does not simulate BLE, TPS43 silicon, or host HID timing.
"""

import argparse
from pathlib import Path
import re
import struct
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zephyr-base", required=True, type=Path)
    parser.add_argument("--left-build", required=True, type=Path)
    parser.add_argument("--right-build", required=True, type=Path)
    parser.add_argument("--aml", action="store_true", help="expect the optional auto mouse layer")
    args = parser.parse_args()
    sys.path.insert(0, str(args.zephyr_base / "scripts/dts/python-devicetree/src"))
    from devicetree import dtlib

    left = dtlib.DT(str(args.left_build / "zephyr/zephyr.dts"))
    right = dtlib.DT(str(args.right_build / "zephyr/zephyr.dts"))
    def cells_of(prop):
        # Mixed phandle/number properties cannot use dtlib's to_nums().
        return list(struct.unpack(">" + "I" * (len(prop.value) // 4), prop.value))

    def config(build):
        return dict(re.findall(r"^(CONFIG_\w+)=(.+)$",
                              (build / "zephyr/.config").read_text(), re.MULTILINE))

    lc, rc = config(args.left_build), config(args.right_build)
    for cfg, build in ((lc, args.left_build), (rc, args.right_build)):
        for name in ("GPIO", "I2C", "INPUT", "ZMK_POINTING", "INPUT_TPS43",
                     "ZMK_INPUT_SPLIT", "ZMK_SPLIT", "ZMK_SLEEP",
                     "ZMK_BATTERY_REPORTING"):
            assert cfg.get("CONFIG_" + name) == "y", name
        assert (build / "zephyr/zmk.uf2").is_file(), "Missing firmware"
    assert lc.get("CONFIG_ZMK_SPLIT_ROLE_CENTRAL") != "y"
    assert rc.get("CONFIG_ZMK_SPLIT_ROLE_CENTRAL") == "y"
    assert rc.get("CONFIG_ZMK_INPUT_LISTENER") == "y"
    for name in ("ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_PROXY",
                 "ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING", "ZMK_STUDIO"):
        assert rc.get("CONFIG_" + name) == "y", name

    def phandle(node):
        return node.props["phandle"].to_num()

    def bindings(dt, node, name, count):
        cells = iter(cells_of(node.props[name]))
        result = []
        for handle in cells:
            target = dt.phandle2node[handle]
            params = [next(cells) for _ in range(target.props[count].to_num())]
            result.append((target, params))
        return result

    for dt in (left, right):
        pad = dt.label2node["tps43"]
        assert pad.props["reg"].to_nums() == [0x74]
        gpio = phandle(dt.label2node["gpio0"])
        for signal, pin in (("rst-gpios", 19), ("rdy-gpios", 15)):
            assert cells_of(pad.props[signal]) == [gpio, pin, 0]
        for mode in ("default", "sleep"):
            group = dt.label2node["nickey44a_tps43_i2c0_" + mode].nodes["group1"]
            assert [v & 0x1ff for v in group.props["psels"].to_nums()] == [9, 33]
        for candidate in dt.node_iter():
            for prop in candidate.props.values():
                if prop.name == "gpios" or prop.name.endswith("-gpios"):
                    cells = cells_of(prop)
                    for handle, pin in zip(cells[::3], cells[1::3]):
                        if handle == gpio and pin in (15, 19):
                            assert candidate is pad
                if prop.name == "psels":
                    assert all(v & 0x1ff not in (15, 19) for v in prop.to_nums())
        for setting in ("single-tap", "two-finger-tap", "scroll", "zoom",
                        "enable-power-management", "switch-xy", "invert-x", "invert-scroll-y"):
            assert setting in pad.props, setting
        assert not {"swipes", "press-and-hold", "invert-scroll-x"} & pad.props.keys()

    ls = left.label2node["left_tps43_split"]
    rs = right.label2node["left_tps43_split"]
    assert ls.props["reg"].to_num() == rs.props["reg"].to_num() == 0
    assert ls.props["device"].to_node() is left.label2node["tps43"]
    assert "device" not in rs.props and "input-processors" not in ls.props
    for listener, device, inertia in (
        ("left_tps43_listener", rs, "left_tps43_inertia"),
        ("tps43_listener", right.label2node["tps43"], "tps43_touch_inertia"),
    ):
        node = right.label2node[listener]
        assert node.props["device"].to_node() is device
        mac = node.nodes["mac"]
        assert mac.props["layers"].to_nums() == [1]
        for mode, mods in ((node, 0x01000000), (mac, 0x08000000)):
            chain = bindings(right, mode, "input-processors", "#input-processor-cells")
            expected = [("pinch_zoom", [mods | 0x7002d, mods | 0x7002e])]
            if mode is mac:
                expected.append(("zip_scroll_transform", [6]))
            expected.extend([("tps43_orientation", [7]), (inertia, [])])
            if args.aml:
                expected.append(("auto_mouse", [8, 30000]))
            assert [(n.name, params) for n, params in chain] == expected

    mouse_keys = right.label2node["mkp_input_listener"]
    if args.aml:
        assert bindings(right, mouse_keys, "input-processors", "#input-processor-cells") == [
            (right.label2node["zip_temp_layer"], [8, 250])]
    else:
        assert "input-processors" not in mouse_keys.props

    layers = list(right.get_node("/keymap").nodes.values())
    assert len(layers) == 17
    for layer in layers:
        keys = bindings(right, layer, "bindings", "#binding-cells")
        assert len(keys) == 44, layer.name
        if layer.name in ("default_win", "default_mac"):
            mac = layer.name == "default_mac"
            expected = {0: ("kp", [0x70029]), 11: ("kp", [0x7002a]),
                        12: ("kp", [0x700e3 if mac else 0x700e0]), 23: ("kp", [0x70028]),
                        24: ("lt", [3 if mac else 2, 0x70091]),
                        37: ("lt", [7 if mac else 6, 0x70090])}
            for idx, (label, params) in expected.items():
                assert keys[idx] == (right.label2node[label], params), (layer.name, idx)
        else:
            for idx in (0, 11, 12, 23, 24, 37):
                label = "bootloader" if layer.name == "kbd_setting" and idx in (24, 37) else "trans"
                assert keys[idx] == (right.label2node[label], []), (layer.name, idx)
        thumb = "studio_unlock" if layer.name == "kbd_setting" else "none"
        assert keys[43] == (right.label2node[thumb], []), layer.name
        for idx in (30, 31):
            assert keys[idx][0] is right.label2node["none"], (layer.name, idx)
    positions = {c.name: c.props["key-positions"].to_nums()
                 for c in right.get_node("/combos").nodes.values()}
    assert positions == {"tab": [14, 15], "shift_tab": [15, 16], "Esc": [1, 2],
                         "middle_click": [19, 20], "caps_lock": [13, 14],
                         "middle_click_alt": [34, 35]}
    print("PASS: both firmware images, pins, taps, scroll, zoom and split routing")
    print("PASS: Windows/Mac shortcuts and scroll transforms on both pads")
    print("PASS: 17 layers, 44 bindings each, unused positions and six combos")


if __name__ == "__main__":
    main()

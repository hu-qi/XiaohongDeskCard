from __future__ import annotations

import unittest

from tools.ws63_board_hook import (
    BEGIN,
    END,
    INIT_ANCHOR,
    POLL_ANCHOR,
    hook,
)

SAMPLE = (
    '#include "disp_driver.h"\n'
    "\n"
    "void LvglTask(void)\n"
    "{\n"
    "    lv_init();\n"
    "    disp_create_ui();\n"
    "    while (1) {\n"
    "        disp_update_ui(msg_recv);\n"
    "    }\n"
    "}\n"
)


class BoardHookTests(unittest.TestCase):
    def test_inserts_init_and_poll_once(self):
        patched = hook(SAMPLE)
        self.assertEqual(patched.count(BEGIN), 3)
        self.assertEqual(patched.count(END), 3)
        self.assertIn('#include "desk_ws63_board.h"', patched)
        self.assertIn("desk_board_init()", patched)
        self.assertIn("desk_board_poll()", patched)

    def test_init_runs_after_ui_and_poll_at_loop_top(self):
        patched = hook(SAMPLE)
        self.assertLess(patched.index("disp_create_ui()"),
                        patched.index("desk_board_init()"))
        # Poll sits at the top of the while body, before the vendor code that
        # may `continue`, so it runs on every iteration.
        poll = patched.index("desk_board_poll()")
        self.assertLess(patched.index("while (1) {"), poll)
        self.assertLess(poll, patched.index("disp_update_ui(msg_recv)"))
        self.assertLess(poll, patched.index("    }\n}\n"))

    def test_is_idempotent(self):
        once = hook(SAMPLE)
        twice = hook(once)
        self.assertEqual(once, twice)

    def test_refuses_when_anchor_missing(self):
        with self.assertRaises(ValueError):
            hook("#include \"disp_driver.h\"\nvoid other(void) {}\n")

    def test_refuses_ambiguous_anchor(self):
        doubled = SAMPLE + SAMPLE
        with self.assertRaises(ValueError):
            hook(doubled)

    def test_refuses_dangling_marker(self):
        with self.assertRaises(ValueError):
            hook(BEGIN + "\n" + SAMPLE)


if __name__ == "__main__":
    unittest.main()

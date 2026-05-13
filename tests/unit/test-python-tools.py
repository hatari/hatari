#!/usr/bin/env python3
#
# Hatari - test-python-tools.py
# Copyright (C) 2026 by manni07
# Created: 2026-05-13
#
# SPDX-License-Identifier: GPL-2.0-or-later

import pathlib
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python-ui"))
sys.path.insert(0, str(ROOT / "tools" / "hconsole"))

conftypes_module = types.ModuleType("conftypes")
conftypes_module.conftypes = {}
sys.modules.setdefault("conftypes", conftypes_module)

from hatari import Hatari as UiHatari  # noqa: E402
import hconsole  # noqa: E402


class HatariPythonToolSecurityTests(unittest.TestCase):
    def test_ui_helper_runs_subprocess_without_shell(self):
        instance = UiHatari.__new__(UiHatari)
        instance.hataribin = sys.executable
        process = instance._run_hatari_command("-c", "print('ui-ok')")
        self.assertIsNotNone(process)
        self.assertEqual(process.returncode, 0)
        self.assertEqual(process.stdout.strip(), "ui-ok")

    def test_ui_tempdir_is_private_and_cleaned_up(self):
        instance = UiHatari(sys.executable)
        tmpdir = pathlib.Path(instance._tmpdir)
        self.assertTrue(tmpdir.is_dir())
        self.assertEqual(pathlib.Path(instance.controlpath).parent, tmpdir)
        instance.kill()
        self.assertFalse(tmpdir.exists())

    def test_hconsole_helper_runs_subprocess_without_shell(self):
        instance = hconsole.Hatari.__new__(hconsole.Hatari)
        instance.hataribin = sys.executable
        process = instance._run_hatari_command("-c", "print('console-ok')")
        self.assertIsNotNone(process)
        self.assertEqual(process.returncode, 0)
        self.assertEqual(process.stdout.strip(), "console-ok")

    def test_invalid_binary_path_is_rejected(self):
        instance = UiHatari.__new__(UiHatari)
        instance.hataribin = "/definitely/not/a/real/hatari"
        self.assertIsNone(instance._resolve_hatari_binary())


if __name__ == "__main__":
    unittest.main()

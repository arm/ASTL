# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).resolve().parents[1] / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


filter_config = load_script('filter_config').filter_config
embed = load_script('embed_config')


class FilterConfigTests(unittest.TestCase):
    def test_linked_overlays_are_filtered_without_modifying_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            root = base / 'config'
            (root / 'stock').mkdir(parents=True)
            (root / 'stock' / 'stock.json').write_text('{}')
            overlay = base / 'overlay'
            overlay.mkdir()
            (overlay / 'allowed.json').write_text('{}')
            (overlay / 'excluded.json').write_text('{}')
            (root / 'customer').symlink_to(overlay, target_is_directory=True)
            (root / 'excluded').symlink_to(overlay, target_is_directory=True)
            (root / 'linked.json').symlink_to(overlay / 'allowed.json')
            filter_config(root, ['stock', 'customer/allowed.json', 'linked.json'])
            self.assertFalse(any(p.is_symlink() for p in root.rglob('*')))
            self.assertEqual(sorted(p.name for p in overlay.iterdir()), ['allowed.json', 'excluded.json'])
            self.assertEqual((overlay / 'excluded.json').read_text(), '{}')
            output = base / 'embedded.cpp'
            embed.generate(root, output, 'customer')
            manifest = json.loads(output.with_suffix('.json').read_text())
            self.assertEqual([r['path'] for r in manifest['resources']],
                             ['customer/allowed.json', 'linked.json', 'stock/stock.json'])

    def test_cycles_fail_without_changing_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'config'
            root.mkdir()
            (root / 'valid.json').write_text('{}')
            (root / 'cycle').symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlink cycle'):
                filter_config(root, ['valid.json'])
            self.assertTrue((root / 'cycle').is_symlink())
            self.assertEqual((root / 'valid.json').read_text(), '{}')

    def test_unmatched_allowlist_preserves_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'config'
            root.mkdir()
            (root / 'valid.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'matches no JSON'):
                filter_config(root, ['missing'])
            self.assertEqual((root / 'valid.json').read_text(), '{}')

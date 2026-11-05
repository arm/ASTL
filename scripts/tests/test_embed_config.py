# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import json
import re
import shutil
import subprocess
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("embed_config", Path(__file__).resolve().parents[1] / "embed_config.py")
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)

class EmbeddedConfigTests(unittest.TestCase):
    def test_minification_preserves_numeric_tokens_and_escaped_strings(self):
        text = json.dumps({"n": 18446744073709551615, "exponent": 1e-12,
                           "s": 'a " b' + chr(92), "unicode": "é"}, ensure_ascii=False)
        compact = embed.minify(text)
        self.assertEqual(json.loads(compact), json.loads(text))
        self.assertIn("18446744073709551615", compact)
        self.assertIn("1e-12", compact)

    def test_generation_tracks_content_additions_removals_and_profiles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "input"
            root.mkdir()
            out = Path(directory) / "generated" / "resources.cpp"
            (root / "a.json").write_text('{ "a": 1 }')
            embed.generate(root, out, "base")
            manifest = json.loads(out.with_suffix('.json').read_text())
            self.assertEqual(manifest['resources'][0]['bytes'], len('{"a":1}'))
            initial = out.read_bytes()
            embed.generate(root, out, "base")
            self.assertEqual(initial, out.read_bytes())
            (root / "b.json").write_text('{"b":2}')
            (root / "a.json").unlink()
            embed.generate(root, out, "customer")
            manifest = json.loads(out.with_suffix('.json').read_text())
            self.assertEqual(manifest['profile'], 'customer')
            self.assertEqual([x['path'] for x in manifest['resources']], ['b.json'])
            self.assertNotIn('a.json', out.read_text())

    def test_large_resource_preserves_bytes_without_string_literals(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'input'
            root.mkdir()
            out = Path(directory) / 'generated' / 'resources.cpp'
            # Exceed both individual and concatenated MSVC string-literal limits.
            text = json.dumps({'large': 'é"\\' * 20000}, ensure_ascii=False)
            (root / 'large.json').write_text(text, encoding='utf-8')
            expected = embed.minify(text).encode('utf-8')
            self.assertGreater(len(expected), 65535)
            embed.generate(root, out, 'large')
            generated = out.read_text()
            initializer = generated.split('constexpr char data_0[] = {', 1)[1].split('};', 1)[0]
            decoded = bytes(int(value, 16) for value in re.findall(r"'\\x([0-9a-f]{2})'", initializer))
            self.assertEqual(decoded, expected)
            self.assertNotIn('"', initializer)
            manifest = json.loads(out.with_suffix('.json').read_text())
            self.assertEqual(manifest['resources'][0]['bytes'], len(expected))
            # Compile and read through the production resource interface when a
            # native C++ driver is available (MSVC compilation runs in Windows CI).
            compiler = shutil.which('c++') or shutil.which('clang++')
            if compiler:
                harness = Path(directory) / 'main.cpp'
                harness.write_text('#include "config/embedded_config.hpp"\n#include <cstdio>\n'
                                   'int main() { auto data = astl::config::EmbeddedResources()[0].data; '
                                   'return std::fwrite(data.data(), 1, data.size(), stdout) == data.size() ? 0 : 1; }\n')
                executable = Path(directory) / 'resource_test'
                subprocess.run([compiler, '-std=c++20', '-I', str(Path(__file__).resolve().parents[2] / 'src/impl'),
                                str(out), str(harness), '-o', str(executable)], check=True, capture_output=True, timeout=60)
                result = subprocess.run([str(executable)], check=True, capture_output=True, timeout=30)
                self.assertEqual(result.stdout, expected)

    def test_malformed_input_fails_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'bad.json').write_text('{')
            with self.assertRaises(json.JSONDecodeError):
                embed.generate(root, root / 'out.cpp', 'bad')

    def test_nonstandard_constants_fail_generation(self):
        for value in ('NaN', 'Infinity', '-Infinity'):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as directory:
                root = Path(directory) / 'input'
                root.mkdir()
                out = Path(directory) / 'generated' / 'resources.cpp'
                (root / 'bad.json').write_text('{"values":[' + value + ']}')
                with self.assertRaisesRegex(ValueError, 'non-standard JSON constant: ' + value):
                    embed.generate(root, out, 'bad')
                self.assertFalse(out.exists())
                self.assertFalse(out.with_suffix('.json').exists())

    def test_constant_names_are_allowed_inside_strings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'input'
            root.mkdir()
            out = Path(directory) / 'generated' / 'resources.cpp'
            (root / 'strings.json').write_text('{"values":["NaN","Infinity","-Infinity"]}')
            embed.generate(root, out, 'valid')
            self.assertTrue(out.exists())

    def test_empty_config_cannot_generate_an_unconfigured_library(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "no JSON resources"):
                embed.generate(root, root / 'out.cpp', 'empty')

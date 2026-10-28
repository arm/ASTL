# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("embed_config", ROOT / "scripts/embed_config.py")
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)


@unittest.skipUnless(all(shutil.which(tool) for tool in ('bash', 'cmake', 'jq', 'zip', 'sha256sum')), 'release tools unavailable')
class EmbeddedReleaseTests(unittest.TestCase):
    def test_profile_packages_omit_config_and_reject_stale_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('scripts/release/stage_release.sh', 'scripts/release/install.sh',
                         'scripts/release/uninstall.sh', 'scripts/publish_configs.sh',
                         'scripts/embed_config.py', 'scripts/filter_config.py', 'scripts/generate_release_manifest.py'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, target)
            for name in ('config/groups', 'config/metrics', 'config/scmi/public', 'doc', 'include/astl', 'mock-bin'):
                (root / name).mkdir(parents=True, exist_ok=True)
            (root / 'config/groups/groups.json').write_text('{}')
            (root / 'config/metrics/metrics.json').write_text('{"metrics":{"stock":{}}}')
            (root / 'doc/api.pdf').write_bytes(b'PDF fixture')
            (root / 'include/astl/astl.h').write_text('/* fixture */')
            for name, data in [('scripts/host_arch.sh', '#!/bin/sh\necho arm64\n'),
                               ('mock-bin/uname', '#!/bin/sh\necho Linux\n')]:
                (root / name).write_text(data)
                (root / name).chmod(0o755)
            env = {**os.environ, 'PATH': str(root / 'mock-bin') + os.pathsep + os.environ['PATH']}
            manifests = []
            for profile in ('first', 'second'):
                (root / 'config/metrics/metrics.json').write_text(json.dumps({'metrics': {profile: {}}}))
                build = root / 'build/release-profiles' / profile / 'release'
                lib = build / 'arm64/lib'
                lib.mkdir(parents=True)
                (lib / 'libastl-0.so').write_bytes(profile.encode())
                (lib / 'libastl_static-0.a').write_bytes(profile.encode())
                (build / 'include/astl').mkdir(parents=True)
                (build / 'include/astl/version.h').write_text('/* version */')
                embed.generate(root / 'config', build / 'generated/embedded_config.cpp', profile)
                hashes = subprocess.check_output(['sha256sum', 'lib/libastl-0.so', 'lib/libastl_static-0.a'], cwd=build / 'arm64')
                (build / 'embedded-binaries.sha256').write_bytes(hashes)
                command = ['bash', str(root / 'scripts/release/stage_release.sh'), '--release-profile', profile,
                           '--version', '1.2.3', '--skip-build', '--skip-tests', '--output-dir', str(root / 'out')]
                result = subprocess.run(command, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
                archive = root / 'out' / f'astl_version_1.2.3_linux_aarch64_{profile}_library.zip'
                with zipfile.ZipFile(archive) as package:
                    self.assertFalse(any('/lib/config/' in name for name in package.namelist()))
                    manifest_name = next(name for name in package.namelist() if name.endswith('/embedded_config.json'))
                    manifests.append(json.loads(package.read(manifest_name)))
                (root / 'config/metrics/metrics.json').write_text('{"metrics":{"changed":{}}}')
                stale = subprocess.run(command, env=env, capture_output=True, text=True)
                self.assertNotEqual(stale.returncode, 0)
                self.assertIn('rebuild required', stale.stderr)
            self.assertNotEqual(manifests[0]['resources'], manifests[1]['resources'])

    def test_replacement_without_allowlist_does_not_write_through_overlay_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('scripts/release/stage_release.sh', 'scripts/embed_config.py',
                         'scripts/filter_config.py'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, target)
            overlay = root / 'overlay'
            overlay.mkdir()
            original = '{"metrics":{"original":{}}}'
            replacement = '{"metrics":{"replacement":{}}}'
            (overlay / 'target.json').write_text(original)
            (overlay / 'replacement.json').write_text(replacement)
            (root / 'mock-bin').mkdir()
            # Simulate a publisher retaining a linked overlay subtree.
            publisher = root / 'scripts/publish_configs.sh'
            publisher.write_text(
                '#!/bin/sh\nset -eu\nmkdir -p "$2"\n'
                'overlay="$(dirname "$0")/../overlay"\n'
                'ln -s "$overlay" "$2/metrics"\n')
            publisher.chmod(0o755)
            for name, data in [('scripts/host_arch.sh', '#!/bin/sh\necho arm64\n'),
                               ('mock-bin/uname', '#!/bin/sh\necho Linux\n')]:
                (root / name).write_text(data)
                (root / name).chmod(0o755)
            env = {**os.environ, 'PATH': str(root / 'mock-bin') + os.pathsep + os.environ['PATH']}
            command = ['bash', str(root / 'scripts/release/stage_release.sh'),
                       '--version', '1.2.3', '--skip-build', '--skip-tests',
                       '--config-replacement', 'metrics/replacement.json:metrics/target.json',
                       '--output-dir', str(root / 'out')]
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            # No binaries were staged: validation should get as far as requiring a build.
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('rebuild required', result.stderr)
            prepared = root / 'build/config-inputs/public'
            self.assertFalse((prepared / 'metrics').is_symlink())
            self.assertEqual((overlay / 'target.json').read_text(), original)
            self.assertEqual((prepared / 'metrics/target.json').read_text(), replacement)
            manifest = json.loads((root / 'build/config-manifests/public/expected.json').read_text())
            hashes = {entry['path']: entry['sha256'] for entry in manifest['resources']}
            self.assertEqual(hashes['metrics/target.json'], hashes['metrics/replacement.json'])

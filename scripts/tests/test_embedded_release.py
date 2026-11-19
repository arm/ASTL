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
    def test_packages_follow_documentation_links_and_require_atx_pdfs(self):
        for layout in ('regular', 'file-links', 'directory-link', 'missing', 'broken-link'):
            with self.subTest(layout=layout), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                for name in ('scripts/release/stage_release.sh', 'scripts/release/install.sh',
                             'scripts/release/uninstall.sh', 'scripts/publish_configs.sh',
                             'scripts/prune_config_references.py', 'scripts/embed_config.py',
                             'scripts/filter_config.py', 'scripts/generate_release_manifest.py'):
                    target = root / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(ROOT / name, target)
                for name in ('config/groups', 'config/metrics', 'config/scmi/public', 'doc',
                             'include/astl', 'mock-bin', 'tools/ATX', 'overlay/doc'):
                    (root / name).mkdir(parents=True, exist_ok=True)
                (root / 'config/metrics/metrics.json').write_text('{"metrics":{"stock":{}}}')
                (root / 'doc/api.pdf').write_bytes(b'Core PDF fixture')
                (root / 'include/astl/astl.h').write_text('/* fixture */')
                atx_docs = root / 'tools/ATX/doc'
                if layout == 'directory-link':
                    atx_docs.symlink_to(root / 'overlay/doc', target_is_directory=True)
                else:
                    atx_docs.mkdir()
                guides = {'user guide.pdf': b'User PDF fixture', 'expert.pdf': b'Expert PDF fixture'}
                for name, content in guides.items():
                    if layout == 'file-links':
                        (root / 'overlay/doc' / name).write_bytes(content)
                        (atx_docs / name).symlink_to(root / 'overlay/doc' / name)
                    elif layout == 'broken-link':
                        (atx_docs / name).symlink_to(root / 'overlay/doc' / name)
                    elif layout != 'missing':
                        (atx_docs / name).write_bytes(content)
                (atx_docs / 'nested').mkdir()
                (atx_docs / 'nested/ignored.pdf').write_bytes(b'Nested PDF fixture')
                for name, data in [('scripts/host_arch.sh', '#!/bin/sh\necho arm64\n'),
                                   ('mock-bin/uname', '#!/bin/sh\necho Linux\n')]:
                    (root / name).write_text(data)
                    (root / name).chmod(0o755)
                build = root / 'build/release-profiles/public/release'
                for name in ('arm64/lib', 'arm64/bin', 'include/astl'):
                    (build / name).mkdir(parents=True)
                (build / 'arm64/lib/libastl-0.so').write_bytes(b'Library fixture')
                (build / 'arm64/lib/libastl_static-0.a').write_bytes(b'Static library fixture')
                (build / 'arm64/bin/atx').write_bytes(b'ATX fixture')
                (build / 'include/astl/version.h').write_text('/* version */')
                embed.generate(root / 'config', build / 'generated/embedded_config.cpp', 'public')
                hashes = subprocess.check_output(
                    ['sha256sum', 'lib/libastl-0.so', 'lib/libastl_static-0.a'], cwd=build / 'arm64')
                (build / 'embedded-binaries.sha256').write_bytes(hashes)
                env = {**os.environ, 'PATH': str(root / 'mock-bin') + os.pathsep + os.environ['PATH']}
                result = subprocess.run([
                    'bash', str(root / 'scripts/release/stage_release.sh'), '--version', '1.2.3',
                    '--variant', 'library-only', '--variant', 'everything', '--variant', 'atx',
                    '--skip-build', '--skip-tests', '--output-dir', str(root / 'out')],
                    env=env, capture_output=True, text=True)
                if layout in ('missing', 'broken-link'):
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('no PDF documentation found', result.stderr)
                    self.assertFalse((root / 'out/atx_version_1.2.3_linux_aarch64.zip').exists())
                    continue
                self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
                for artifact, doc_path in (
                    ('astl_version_1.2.3_linux_aarch64_library', 'doc/ATX'),
                    ('astl_version_1.2.3_linux_aarch64_everything', 'doc/ATX'),
                    ('atx_version_1.2.3_linux_aarch64', 'doc'),
                ):
                    with zipfile.ZipFile(root / 'out' / f'{artifact}.zip') as package:
                        for name, content in guides.items():
                            self.assertEqual(package.read(f'{artifact}/{doc_path}/{name}'), content)
                            self.assertFalse((root / 'out' / artifact / doc_path / name).is_symlink())
                        self.assertFalse(any(name.endswith('/ignored.pdf') for name in package.namelist()))

    def test_replacement_restores_a_filtered_base_before_pruning(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('scripts/release/stage_release.sh', 'scripts/publish_configs.sh',
                         'scripts/prune_config_references.py', 'scripts/embed_config.py', 'scripts/filter_config.py'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, target)
            metrics = root / 'config/metrics'
            metrics.mkdir(parents=True)
            (root / 'config/scmi/public').mkdir(parents=True)
            (metrics / 'base.json').write_text(json.dumps({'document': {'confidential': True}}))
            child = {'extends': 'base.json', 'metrics': {'retained': {}}}
            (metrics / 'child.json').write_text(json.dumps(child))
            (metrics / 'replacement.json').write_text('{}')
            (root / 'mock-bin').mkdir()
            for name, data in [('scripts/host_arch.sh', '#!/bin/sh\necho arm64\n'),
                               ('mock-bin/uname', '#!/bin/sh\necho Linux\n')]:
                (root / name).write_text(data)
                (root / name).chmod(0o755)
            env = {**os.environ, 'PATH': str(root / 'mock-bin') + os.pathsep + os.environ['PATH']}
            result = subprocess.run([
                'bash', str(root / 'scripts/release/stage_release.sh'), '--version', '1.2.3',
                '--skip-build', '--skip-tests', '--config-replacement', 'metrics/replacement.json:metrics/base.json',
                '--output-dir', str(root / 'out')], env=env, capture_output=True, text=True)
            # Input preparation must succeed and reach the missing-binary check.
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('rebuild required', result.stderr)
            prepared = root / 'build/config-inputs/public/metrics'
            self.assertEqual(json.loads((prepared / 'child.json').read_text()), child)
            self.assertEqual(json.loads((prepared / 'base.json').read_text()), {})

    def test_profile_packages_omit_config_and_reject_stale_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('scripts/release/stage_release.sh', 'scripts/release/install.sh',
                         'scripts/release/uninstall.sh', 'scripts/publish_configs.sh',
                         'scripts/prune_config_references.py',
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

    @unittest.skipUnless(all(shutil.which(tool) for tool in ('cmake', 'ctest')), 'CMake/CTest unavailable')
    def test_staging_tests_the_profile_and_rejects_failed_or_empty_suites(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('scripts/release/stage_release.sh', 'scripts/release/install.sh',
                         'scripts/release/uninstall.sh', 'scripts/publish_configs.sh',
                         'scripts/prune_config_references.py', 'scripts/embed_config.py',
                         'scripts/filter_config.py', 'scripts/generate_release_manifest.py'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, target)
            for name in ('config/groups', 'config/metrics', 'config/scmi/public', 'doc', 'include/astl', 'mock-bin'):
                (root / name).mkdir(parents=True, exist_ok=True)
            (root / 'config/metrics/metrics.json').write_text('{"metrics":{"stock":{}}}')
            (root / 'doc/api.pdf').write_bytes(b'PDF fixture')
            (root / 'include/astl/astl.h').write_text('/* fixture */')
            for name, data in [('scripts/host_arch.sh', '#!/bin/sh\necho arm64\n'),
                               ('mock-bin/uname', '#!/bin/sh\necho Linux\n')]:
                (root / name).write_text(data)
                (root / name).chmod(0o755)
            build = root / 'build/release-profiles/public/release'
            for name in ('arm64/lib', 'include/astl'):
                (build / name).mkdir(parents=True)
            (build / 'arm64/lib/libastl-0.so').write_bytes(b'Library fixture')
            (build / 'arm64/lib/libastl_static-0.a').write_bytes(b'Static library fixture')
            (build / 'include/astl/version.h').write_text('/* version */')
            embed.generate(root / 'config', build / 'generated/embedded_config.cpp', 'public')
            hashes = subprocess.check_output(
                ['sha256sum', 'lib/libastl-0.so', 'lib/libastl_static-0.a'], cwd=build / 'arm64')
            (build / 'embedded-binaries.sha256').write_bytes(hashes)
            cmake = json.dumps(shutil.which('cmake'))
            ordinary_build = root / 'build/release'
            ordinary_build.mkdir()
            (ordinary_build / 'CTestTestfile.cmake').write_text(f'add_test(wrong_tree {cmake} -E false)\n')
            # The release preset selects the unrelated normal build tree.
            presets = root / 'CMakePresets.json'
            shutil.copy2(ROOT / 'CMakePresets.json', presets)
            marker = build / 'profile-test-ran'
            test_file = build / 'CTestTestfile.cmake'
            env = {**os.environ, 'PATH': str(root / 'mock-bin') + os.pathsep + os.environ['PATH']}
            cases = {
                'pass': f'add_test(profile_test {cmake} -E touch {json.dumps(str(marker))})\n',
                'fail': f'add_test(profile_test {cmake} -E false)\n',
                'empty': '',
                'without-presets': f'add_test(profile_test {cmake} -E touch {json.dumps(str(marker))})\n',
            }
            for outcome, tests in cases.items():
                with self.subTest(outcome=outcome):
                    marker.unlink(missing_ok=True)
                    # A configured build also works without source presets and
                    # from an external caller directory (confidential staging).
                    if outcome == 'without-presets':
                        presets.unlink()
                    test_file.write_text(tests)
                    output = root / 'out' / outcome
                    result = subprocess.run([
                        'bash', str(root / 'scripts/release/stage_release.sh'), '--version', '1.2.3',
                        '--skip-build', '--output-dir', str(output)], cwd=root.parent,
                        env=env, capture_output=True, text=True)
                    archive = output / 'astl_version_1.2.3_linux_aarch64_library.zip'
                    if outcome in ('pass', 'without-presets'):
                        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
                        self.assertTrue(marker.exists(), result.stdout)
                        self.assertTrue(archive.exists())
                    else:
                        self.assertNotEqual(result.returncode, 0, result.stdout)
                        self.assertFalse(archive.exists())
                        diagnostic = 'profile_test' if outcome == 'fail' else 'No tests were found'
                        self.assertIn(diagnostic, result.stderr + result.stdout)

    def test_replacement_without_allowlist_does_not_write_through_overlay_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('scripts/release/stage_release.sh', 'scripts/embed_config.py',
                         'scripts/filter_config.py', 'scripts/prune_config_references.py'):
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
                'ln -s "$overlay" "$2/metrics"\n'
                'while [ "$#" -gt 0 ]; do\n'
                '  if [ "$1" = "--defer-reference-pruning" ]; then\n'
                '    mkdir -p "$(dirname "$2")"; : > "$2"; break\n'
                '  fi\n'
                '  shift\n'
                'done\n')
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

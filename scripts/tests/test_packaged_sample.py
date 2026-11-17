# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which(os.environ.get('CXX', 'c++'))


@unittest.skipUnless(os.name == 'posix' and COMPILER and shutil.which('cmake') and shutil.which('make'),
                     'standalone sample compilation requires a native POSIX C++ toolchain')
class PackagedSampleTests(unittest.TestCase):
    def test_sample_compiles_with_only_packaged_headers_in_both_standards(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            shutil.copytree(ROOT / 'samples/sample_test', package / 'samples/sample_test')
            shutil.copytree(ROOT / 'cmake/Modules', package / 'cmake/Modules')
            headers = package / 'include/astl'
            headers.mkdir(parents=True)
            for header in (ROOT / 'include/astl').glob('*.h'):
                shutil.copy2(header, headers / header.name)
            version = (ROOT / 'include/astl/astl_version.h.in').read_text()
            for name, value in (('ASTL_VERSION', '1.2.3'), ('PROJECT_VERSION_MAJOR', '1'),
                                ('PROJECT_VERSION_MINOR', '2'), ('PROJECT_VERSION_PATCH', '3')):
                version = version.replace(f'@{name}@', value)
            (headers / 'astl_version.h').write_text(version)
            # Configuration needs an imported-library location. Compile the real
            # sample translation unit; linking and execution use release OOB CI.
            library = package / 'lib/libastl-0.so'
            library.parent.mkdir()
            library.touch()
            for standard in ('20', '23'):
                with self.subTest(standard=standard):
                    build = package / f'build-cxx{standard}'
                    configured = subprocess.run([
                        'cmake', '-G', 'Unix Makefiles', '-S', str(package / 'samples/sample_test'),
                        '-B', str(build), f'-DCMAKE_CXX_STANDARD={standard}',
                        f'-DCMAKE_CXX_COMPILER={COMPILER}', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                        f'-DASTL_INSTALL_INCLUDE_DIR={package / "include"}',
                        f'-DASTL_INSTALL_LIB_DIR={library}',
                        '-DCMAKE_DISABLE_FIND_PACKAGE_tl-expected=ON',
                        '-DCMAKE_DISABLE_FIND_PACKAGE_tl-optional=ON',
                    ], capture_output=True, text=True, timeout=60)
                    self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
                    commands = json.loads((build / 'compile_commands.json').read_text())
                    self.assertEqual(len(commands), 1)
                    command = commands[0]
                    compiled = subprocess.run(shlex.split(command['command']), cwd=command['directory'],
                                              capture_output=True, text=True, timeout=60)
                    self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)

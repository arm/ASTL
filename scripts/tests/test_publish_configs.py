# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
"""Published config must remain usable after confidential files are excluded."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPTS = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('prune_config_references', SCRIPTS / 'prune_config_references.py')
PRUNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PRUNER)


class ReferencePruningTests(unittest.TestCase):
    def test_both_mapping_types_keep_public_references(self):
        for field, reference_key in (('uuid_mapping', 'specification_file'), ('scmi_uuid_mapping', 'metrics_file')):
            with self.subTest(field=field):
                documents = {'device/meta.json': {field: {
                    'public': {reference_key: 'public.json'}, 'private': {reference_key: 'private.json'}}},
                    'device/public.json': {}}
                result = PRUNER.prune_references(documents, {'device/private.json'})
                self.assertEqual(set(result['device/meta.json'][field]), {'public'})
                self.assertIn('private', documents['device/meta.json'][field])

    def test_inheritance_exclusions_propagate_to_mappings(self):
        documents = {
            'child.json': {'extends': 'base.json'},
            'base.json': {'extends': 'private.json'},
            'meta.json': {'uuid_mapping': {'child': {'specification_file': 'child.json'}}},
        }
        result = PRUNER.prune_references(documents, {'private.json'})
        self.assertEqual(result, {'meta.json': {'uuid_mapping': {}}})

    def test_missing_references_are_not_silently_removed(self):
        for doc in ({'extends': 'missing.json'},
                    {'uuid_mapping': {'broken': {'specification_file': 'missing.json'}}}):
            with self.subTest(doc=doc), self.assertRaisesRegex(ValueError, 'missing published config reference'):
                PRUNER.prune_references({'meta.json': doc}, set())

    def test_references_cannot_escape_the_published_tree(self):
        for reference in ('../private.json', '/private.json', 42):
            with self.subTest(reference=reference), self.assertRaises(ValueError):
                PRUNER.prune_references({'meta.json': {'extends': reference}}, {'private.json'})

    def test_no_exclusions_preserve_all_references(self):
        documents = {'base.json': {}, 'child.json': {'extends': 'base.json'},
                     'meta.json': {'uuid_mapping': {'child': {'specification_file': 'child.json'}}}}
        self.assertEqual(PRUNER.prune_references(documents, set()), documents)


@unittest.skipUnless(shutil.which('jq') and shutil.which('bash'), 'Publisher requires bash and jq')
class PublisherTests(unittest.TestCase):
    def test_public_and_confidential_publication_close_references(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scripts = root / 'scripts'
            scripts.mkdir()
            for name in ('publish_configs.sh', 'prune_config_references.py'):
                shutil.copy2(SCRIPTS / name, scripts / name)
            source = root / 'config' / 'scmi' / 'public' / 'device'
            source.mkdir(parents=True)
            mapping = {'uuid_mapping': {
                'public': {'specification_file': 'public.json'},
                'private': {'specification_file': 'private.json'},
                'private_top': {'specification_file': 'private_top.json'},
            }}
            (source / 'repometa.json').write_text(json.dumps(mapping), encoding='utf-8')
            (source / 'public.json').write_text('{}', encoding='utf-8')
            (source / 'private.json').write_text(json.dumps({'document': {'confidential': True}}), encoding='utf-8')
            (source / 'private_top.json').write_text(json.dumps({'confidential': True}), encoding='utf-8')
            output = root / 'output with spaces'
            command = ['bash', str(scripts / 'publish_configs.sh'), '-o', str(output)]
            # Publishing public content over a prior confidential tree must also
            # remove stale private documents and the references to them.
            subprocess.run(command + ['--confidential'], check=True, capture_output=True, text=True, timeout=30)
            published = output / 'scmi' / 'public' / 'device'
            self.assertEqual((published / 'repometa.json').read_bytes(), (source / 'repometa.json').read_bytes())
            self.assertTrue((published / 'private.json').is_file())
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)
            result = json.loads((published / 'repometa.json').read_text())
            self.assertEqual(set(result['uuid_mapping']), {'public'})
            self.assertFalse((published / 'private.json').exists())
            self.assertFalse((published / 'private_top.json').exists())
            self.assertEqual(json.loads((source / 'repometa.json').read_text()), mapping)
            # A typo is not a confidentiality exclusion: publishing must fail.
            mapping['uuid_mapping']['broken'] = {'specification_file': 'missing.json'}
            (source / 'repometa.json').write_text(json.dumps(mapping), encoding='utf-8')
            failed = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn('missing published config reference', failed.stderr)

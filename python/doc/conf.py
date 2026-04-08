# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import os
import sys
from datetime import datetime

# Add package root (../) so Sphinx can import astl
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

project = 'ASTL Python API'
author = 'Arm'
copyright = f'{datetime.now():%Y}, {author}'

extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.napoleon',
    'sphinx.ext.autosummary',
    'sphinx.ext.viewcode',
    'myst_parser',  # markdown support
]

autosummary_generate = True
autodoc_typehints = 'description'
napoleon_google_docstring = True
napoleon_numpy_docstring = True

html_theme = 'alabaster'
html_static_path = ['_static']

# MyST configuration (allow basic extended syntax)
myst_enable_extensions = [
    'colon_fence',
    'deflist',
]
exclude_patterns = ['_build']

# Fail gracefully if optional deps missing
autodoc_mock_imports = ['pandas']

# Keep ordering as in source for readability
autodoc_member_order = 'bysource'

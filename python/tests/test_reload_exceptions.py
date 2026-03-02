# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import importlib, sys
import astl
from astl import Status, InitializationError, map_status_to_exception

def test_reload_preserves_mapping_for_not_initialized():
    # Sanity precondition
    assert map_status_to_exception(Status.NOT_INITIALIZED) is InitializationError
    # Remove module and reload
    sys.modules.pop('astl.exceptions', None)
    importlib.import_module('astl.exceptions')  # re-import
    # Re-import mapping function to ensure we get the possibly reloaded implementation
    from astl import map_status_to_exception as m
    assert m(Status.NOT_INITIALIZED) is InitializationError

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import pytest

from astl import (
    ASTLError,
    InvalidArgumentError,
    BadArgumentError,
    NotSupportedError,
    InternalError,
    map_status_to_exception,
    Status,
)


def test_specialized_exceptions_share_exported_base():
    assert issubclass(InternalError, ASTLError)
    assert isinstance(InternalError(Status.INTERNAL_ERROR), ASTLError)


def test_map_status_to_exception_known_codes():
    # BAD_ARGUMENT should map now
    if hasattr(Status, 'BAD_ARGUMENT'):
        assert map_status_to_exception(Status.BAD_ARGUMENT) in (BadArgumentError, InvalidArgumentError)
    # INVALID_ARGUMENT may exist; if so ensure mapping
    if hasattr(Status, 'INVALID_ARGUMENT'):
        assert map_status_to_exception(Status.INVALID_ARGUMENT) is InvalidArgumentError
    if hasattr(Status, 'NOT_SUPPORTED'):
        assert map_status_to_exception(Status.NOT_SUPPORTED) is NotSupportedError
    if hasattr(Status, 'INTERNAL_ERROR'):
        assert map_status_to_exception(Status.INTERNAL_ERROR) is InternalError

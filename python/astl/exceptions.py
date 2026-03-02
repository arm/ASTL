# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Custom exception hierarchy for ASTL Python bindings.

Purpose:
        Provide semantic Python exception subclasses mapped from integer status
        codes surfaced by the C layer. This lets user code catch *meaningful* error
        categories (``InitializationError`` etc.) while preserving a single base
        class (``ASTLError``) for broad handling.

Mapping policy:
    * Unknown / unmapped status codes fall back to the raw ``ASTLError`` raised
        by the underlying binding layer (caller can still inspect the status code).
    * New mappings are added in a backwards compatible way—callers written today
        that catch ``ASTLError`` continue to function when more granular subclasses
        arrive later.
    * Naming avoids shadowing built-in Python exceptions (e.g. ``NotImplementedError``)
        by suffixing with ``ASTL`` when clarity beats brevity.
"""
from __future__ import annotations

from typing import Type, Dict

try:  # Avoid circular import during _core initialization
    from ._core import ASTLError, Status  # type: ignore
except Exception:  # pragma: no cover - fallback path during partial init
    class ASTLError(Exception):  # type: ignore
        """Fallback base when _core not yet fully initialized."""
        pass

    class Status:  # type: ignore
        """Fallback placeholder; getattr(Status, NAME, -1) safely returns -1."""
        pass


class InitializationError(ASTLError):
    """Raised when operations are attempted before successful initialize()."""
    def __init__(self, *args):  # type: ignore[override]
        """Augment message with user guidance.

        Tests raise ``InitializationError(Status.NOT_INITIALIZED)`` directly; the
        base ``ASTLError`` would normally turn an int into just that numeric in
        the .args, so we provide a clearer human‑readable message.
        """
        if len(args) == 1 and isinstance(args[0], int):
            code = int(args[0])
            super().__init__(f"Operation requires initialize() to be called first (status code {code})")
        else:  # Pass through any richer message/signature combinations
            super().__init__(*args)  # pragma: no cover - exercised indirectly


class NotImplementedErrorASTL(ASTLError):  # distinguish from built-in NotImplementedError
    """Raised for NOT_IMPLEMENTED status when not suppressed."""


class InvalidArgumentError(ASTLError):
    """Raised when ASTL reports INVALID_ARGUMENT."""


class OutOfMemoryError(ASTLError):
    """Raised when allocation fails in the underlying library."""


class BadArgumentError(ASTLError):
    """Raised when ASTL reports BAD_ARGUMENT (pre-validation failure)."""


class NotSupportedError(ASTLError):
    """Raised when an operation isn't supported on the current target/platform."""


class DeprecatedAPIError(ASTLError):
    """Raised when a deprecated API path is invoked."""


class InternalError(ASTLError):
    """Raised for internal errors reported by the native library."""


_STATUS_MAP: Dict[int, Type[ASTLError]] = {}


def _build_status_map() -> None:
    """Populate the internal status -> exception mapping once.

    We resolve integer constants dynamically from ``Status`` so that this module can be
    imported before the C extension has fully initialized (fallback placeholder may be
    present). Only integer-valued attributes are added. Subsequent calls are no-ops.
    """
    if _STATUS_MAP:  # Already built
        return
    # Mapping table (attribute name on Status -> exception subclass)
    spec: Dict[str, Type[ASTLError]] = {
        'NOT_INITIALIZED': InitializationError,
        'NOT_IMPLEMENTED': NotImplementedErrorASTL,
        'INVALID_ARGUMENT': InvalidArgumentError,
        'BAD_ARGUMENT': BadArgumentError,
        'OUT_OF_MEMORY': OutOfMemoryError,
        'NOT_SUPPORTED': NotSupportedError,
        'DEPRECATED_API': DeprecatedAPIError,
        'INTERNAL_ERROR': InternalError,
    }
    for attr, exc_cls in spec.items():
        value = getattr(Status, attr, None)
        if isinstance(value, int):  # Only map realized integer constants
            _STATUS_MAP.setdefault(value, exc_cls)


def _ensure_real_status() -> None:
    """Rebind global Status to the real implementation if a fallback placeholder is present.

    During early import ordering adjustments we introduced a fallback stub to avoid
    circular import errors. In certain edge cases (notably when other modules
    import this module before ``astl._core`` is fully initialized) the stub could
    persist, leaving mapping logic unable to see the integer constants. This
    helper detects that situation (by absence of a known attribute) and patches
    in the genuine class.
    """
    global Status
    if not hasattr(Status, 'NOT_INITIALIZED') or not isinstance(getattr(Status, 'NOT_INITIALIZED', None), int):
        try:  # pragma: no cover - defensive path
            from ._core import Status as RealStatus  # type: ignore
            if hasattr(RealStatus, 'NOT_INITIALIZED'):
                # Rebinding the imported Status symbol at runtime is intentional to swap
                # the provisional placeholder with the real extension class. Mypy flags
                # this as "Cannot assign to a type" (misc); safe to ignore here.
                Status = RealStatus  # type: ignore[misc]
        except Exception:
            pass


def map_status_to_exception(status_code: int) -> Type[ASTLError] | None:
    """Return a specialized exception subclass for a status code if known.

    Lazily builds a dictionary mapping integer status codes (resolved from the real
    ``Status`` enum when available) to their Python exception subclass. Falls back to
    ``None`` when the status code has no specialized mapping.
    """
    _ensure_real_status()
    _build_status_map()
    return _STATUS_MAP.get(status_code)


__all__ = [
    "InitializationError",
    "NotImplementedErrorASTL",
    "InvalidArgumentError",
    "OutOfMemoryError",
    "BadArgumentError",
    "NotSupportedError",
    "DeprecatedAPIError",
    "InternalError",
    "map_status_to_exception",
]

"""Basic smoke test for streaming helpers.

Focus:
    * Ensure new helpers are exported on the top-level package
    * Exercise a single poll invocation if at least one counter is present
    * Remain tolerant of empty environments (no targets) without failing
"""
import astl


def test_streaming_helpers_import_and_graceful():
    # Ensure helpers are accessible
    assert hasattr(astl, 'poll_counter_once')
    assert hasattr(astl, 'stream_counter')

    targets = astl.get_targets()
    if not targets:
        # Nothing further to test; helper presence is enough when no hardware
        return
    t = targets[0]
    counters = astl.get_counters(t)
    if counters:
        c = counters[0]
        res = astl.poll_counter_once(t, c)
        assert hasattr(res, 'samples')

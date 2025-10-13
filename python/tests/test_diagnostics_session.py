import astl
from astl import diagnostics, Session


def test_diagnostics_keys():
    info = diagnostics()
    d = info.to_dict()
    # Required keys
    for key in [
        "python_version",
        "platform",
        "implementation",
        "executable",
        "cwd",
        "astl_version",
        "target_count",
        "env_astl_config",
    ]:
        assert key in d


def test_session_no_targets():
    astl.initialize(None)
    targets = astl.get_targets()
    # Always should work even if no targets
    with Session(target=targets[0] if targets else None, counters=[], metrics=[]) as sess:
        snap = sess.poll_once()
        assert set(snap.keys()) == {"counters", "metrics"}
        assert isinstance(snap["counters"], dict)
        assert isinstance(snap["metrics"], dict)

import astl

def test_version_struct():
    maj, min_, mic, s = astl.version()
    assert isinstance(maj, int) and isinstance(min_, int) and isinstance(mic, int)
    assert s.count('.') >= 1


def test_empty_enumerations_do_not_error():
    astl.initialize(None)
    targets = astl.get_targets()
    assert isinstance(targets, list)
    if targets:
        t = targets[0]
        counters = astl.get_counters(t)
        metrics = astl.get_metrics(t)
        groups = astl.get_metric_groups(t)
        # Types and list semantics
        assert isinstance(counters, list)
        assert isinstance(metrics, list)
        assert isinstance(groups, list)

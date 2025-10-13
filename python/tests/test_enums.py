from astl import Units, ValueType, CounterType, MetricType


def test_enum_members_exist():
    # Smoke test a few representative members
    assert Units.WATTS.value == 5  # Depends on ordering defined in binding
    assert ValueType.UINT64.value == 3
    assert CounterType.EVENT.value == 2
    assert MetricType.RATE.value == 5

    # Ensure UNKNOWN values present
    assert Units.UNKNOWN.name == "UNKNOWN"
    assert ValueType.UNKNOWN.name == "UNKNOWN"
    assert CounterType.UNKNOWN.name == "UNKNOWN"
    assert MetricType.UNKNOWN.name == "UNKNOWN"

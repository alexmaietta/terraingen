import logging

import pytest

import terraingen as tg


@pytest.fixture(autouse=True)
def restore_logging():
    yield
    tg.use_python_logging()
    tg.set_log_level("warning")


def test_log_level_round_trips_and_rejects_unknown_names():
    assert tg.get_log_level() == "warning"
    tg.set_log_level("DEBUG")
    assert tg.get_log_level() == "debug"
    with pytest.raises(ValueError):
        tg.set_log_level("loud")


def test_records_reach_the_logging_module(caplog):
    tg.set_log_level("info")
    with caplog.at_level(logging.INFO, logger="terraingen"):
        tg.generate_synthetic_terrain(9, 7, 1.0, tg.SyntheticTerrainParams())
    records = [r for r in caplog.records if r.name == "terraingen.noise"]
    assert len(records) == 1
    assert records[0].levelno == logging.INFO
    assert records[0].getMessage().startswith("Filled 9x7 synthetic terrain (1 layer) in ")


def test_nothing_is_logged_below_the_threshold(caplog):
    with caplog.at_level(logging.DEBUG, logger="terraingen"):
        tg.generate_synthetic_terrain(4, 4, 1.0, tg.SyntheticTerrainParams())
    assert not [r for r in caplog.records if r.name.startswith("terraingen")]


def test_custom_sinks_receive_level_category_and_message():
    received = []
    tg.set_log_sink(lambda level, category, message: received.append((level, category, message)))
    tg.set_log_level("info")
    tg.generate_synthetic_terrain(3, 3, 1.0, tg.SyntheticTerrainParams())
    assert len(received) == 1
    level, category, message = received[0]
    assert (level, category) == ("info", "noise")
    assert message.startswith("Filled 3x3")

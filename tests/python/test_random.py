import math

import numpy as np
import pytest

import terraingen as tg


def ks_statistic(samples, cdf):
    samples = np.sort(np.asarray(samples, dtype=np.float64))
    n = len(samples)
    f = np.array([cdf(x) for x in samples])
    return max(np.max(f - np.arange(n) / n), np.max(np.arange(1, n + 1) / n - f))


def test_philox_matches_random123_known_answers():
    assert tg.philox4x32(0, 0, 0, 0, 0, 0) == (0x6627E8D5, 0xE169C58D, 0xBC57AC4C, 0x9B00DBD8)
    assert tg.philox4x32(0x243F6A88, 0x85A308D3, 0x13198A2E, 0x03707344, 0xA4093822, 0x299F31D0) == (
        0xD16CFE09, 0x94FDCCEB, 0x5001E420, 0x24126EA1)


def test_stream_reads_consecutive_philox_blocks():
    stream = tg.RandomStream(11, 22, 1, 2, 3)
    first = tg.philox4x32(1, 2, 3, 0, 11, 22)
    second = tg.philox4x32(1, 2, 3, 1, 11, 22)
    assert [stream.next_u32() for _ in range(6)] == list(first) + list(second[:2])


def test_array_draws_match_scalar_draws():
    words = tg.RandomStream(5, 6).u32s(10)
    scalar = tg.RandomStream(5, 6)
    assert words.dtype == np.uint32
    assert list(words) == [scalar.next_u32() for _ in range(10)]

    doubles = tg.RandomStream(5, 7).doubles(10)
    scalar = tg.RandomStream(5, 7)
    np.testing.assert_array_equal(doubles, [scalar.next_double() for _ in range(10)])


def test_uniform_ranges():
    stream = tg.RandomStream(1, 1)
    for _ in range(1000):
        assert 0.0 <= stream.next_float() < 1.0
        assert 0.0 <= stream.next_double() < 1.0
        assert 0.0 < stream.next_double_open_below() <= 1.0


def test_substreams_differ():
    assert list(tg.RandomStream(1, 2, 0, 0, 0).u32s(4)) != list(tg.RandomStream(1, 2, 1, 0, 0).u32s(4))


def test_samplers_return_scalars_or_arrays():
    stream = tg.RandomStream(2, 2)
    assert isinstance(tg.sample_poisson(stream, 3.0), int)
    counts = tg.sample_poisson(stream, 3.0, count=5)
    assert counts.shape == (5,) and counts.dtype == np.uint64
    assert isinstance(tg.sample_normal(stream, 0.0, 1.0), float)
    assert tg.sample_exponential(stream, 2.0, count=3).shape == (3,)


def test_samplers_reject_invalid_arguments():
    stream = tg.RandomStream(3, 3)
    with pytest.raises(ValueError):
        tg.sample_poisson(stream, -1.0)
    with pytest.raises(ValueError):
        tg.sample_truncated_power_law(stream, 0.0, 10.0, 2.0)
    with pytest.raises(ValueError):
        tg.sample_exponential(stream, 0.0)
    with pytest.raises(ValueError):
        tg.sample_normal(stream, 0.0, -1.0)
    with pytest.raises(ValueError):
        tg.TabulatedDistribution([1.0, 2.0], [0.0, 0.5])


def test_poisson_mean_and_variance():
    for mean in (0.5, 4.0, 25.0, 300.0):
        counts = tg.sample_poisson(tg.RandomStream(4, int(mean * 10)), mean, count=40000).astype(np.float64)
        assert counts.mean() == pytest.approx(mean, abs=5 * math.sqrt(mean / 40000))
        assert counts.var(ddof=1) / mean == pytest.approx(1.0, abs=0.05)


def test_truncated_power_law_matches_its_cdf():
    samples = tg.sample_truncated_power_law(tg.RandomStream(5, 5), 1.0, 100.0, 1.8, count=20000)
    assert samples.min() >= 1.0 and samples.max() <= 100.0
    low, high = 1.0, 100.0 ** -1.8
    d = ks_statistic(samples, lambda x: (low - x ** -1.8) / (low - high))
    assert d < 1.949 / math.sqrt(len(samples))


def test_tabulated_distribution_cdf_and_sampling():
    table = tg.TabulatedDistribution([0.0, 1.0, 5.0], [0.0, 0.25, 1.0])
    assert table.cdf(-1.0) == 0.0
    assert table.cdf(0.5) == pytest.approx(0.125)
    assert table.cdf(10.0) == 1.0
    samples = table.sample(tg.RandomStream(6, 6), count=20000)
    assert samples.min() >= 0.0 and samples.max() <= 5.0
    assert ks_statistic(samples, table.cdf) < 1.949 / math.sqrt(len(samples))
    assert 0.0 <= table.sample(tg.RandomStream(6, 7)) <= 5.0

"""random: Philox streams, samplers and tabulated distributions."""

import math

import numpy as np
import pytest

import terraingen


def ks_statistic(samples, cdf):
    samples = np.sort(np.asarray(samples, dtype=np.float64))
    n = len(samples)
    f = np.array([cdf(x) for x in samples])
    return max(np.max(f - np.arange(n) / n), np.max(np.arange(1, n + 1) / n - f))


def test_philox_matches_random123_known_answers():
    assert terraingen.philox4x32(0, 0, 0, 0, 0, 0) == (0x6627E8D5, 0xE169C58D, 0xBC57AC4C, 0x9B00DBD8)
    assert terraingen.philox4x32(0x243F6A88, 0x85A308D3, 0x13198A2E, 0x03707344, 0xA4093822, 0x299F31D0) == (
        0xD16CFE09,
        0x94FDCCEB,
        0x5001E420,
        0x24126EA1,
    )


def test_stream_reads_consecutive_philox_blocks():
    stream = terraingen.RandomStream(11, 22, 1, 2, 3)
    first = terraingen.philox4x32(1, 2, 3, 0, 11, 22)
    second = terraingen.philox4x32(1, 2, 3, 1, 11, 22)
    assert [stream.next_u32() for _ in range(6)] == list(first) + list(second[:2])


def test_array_words_match_scalar_words():
    words = terraingen.RandomStream(5, 6).u32s(10)
    scalar = terraingen.RandomStream(5, 6)
    assert words.dtype == np.uint32
    assert list(words) == [scalar.next_u32() for _ in range(10)]


def test_array_doubles_match_scalar_doubles():
    doubles = terraingen.RandomStream(5, 7).doubles(10)
    scalar = terraingen.RandomStream(5, 7)
    np.testing.assert_array_equal(doubles, [scalar.next_double() for _ in range(10)])


def test_uniform_ranges():
    stream = terraingen.RandomStream(1, 1)
    for _ in range(1000):
        assert 0.0 <= stream.next_float() < 1.0
        assert 0.0 <= stream.next_double() < 1.0
        assert 0.0 < stream.next_double_open_below() <= 1.0


def test_substreams_differ():
    first = terraingen.RandomStream(1, 2, 0, 0, 0).u32s(4)
    assert list(first) != list(terraingen.RandomStream(1, 2, 1, 0, 0).u32s(4))


def test_samplers_return_scalars_or_arrays():
    stream = terraingen.RandomStream(2, 2)
    assert isinstance(terraingen.sample_poisson(stream, 3.0), int)
    counts = terraingen.sample_poisson(stream, 3.0, count=5)
    assert counts.shape == (5,) and counts.dtype == np.uint64
    assert isinstance(terraingen.sample_normal(stream, 0.0, 1.0), float)
    assert terraingen.sample_exponential(stream, 2.0, count=3).shape == (3,)


def test_samplers_reject_invalid_arguments():
    stream = terraingen.RandomStream(3, 3)
    with pytest.raises(ValueError):
        terraingen.sample_poisson(stream, -1.0)
    with pytest.raises(ValueError):
        terraingen.sample_truncated_power_law(stream, 0.0, 10.0, 2.0)
    with pytest.raises(ValueError):
        terraingen.sample_exponential(stream, 0.0)
    with pytest.raises(ValueError):
        terraingen.sample_normal(stream, 0.0, -1.0)
    with pytest.raises(ValueError):
        terraingen.TabulatedDistribution([1.0, 2.0], [0.0, 0.5])


@pytest.mark.parametrize("mean", [0.5, 4.0, 25.0, 300.0])
def test_poisson_mean_and_variance(mean):
    counts = terraingen.sample_poisson(terraingen.RandomStream(4, int(mean * 10)), mean, count=40000)
    counts = counts.astype(np.float64)
    assert counts.mean() == pytest.approx(mean, abs=5 * math.sqrt(mean / 40000))
    assert counts.var(ddof=1) / mean == pytest.approx(1.0, abs=0.05)


def test_truncated_power_law_matches_its_cdf():
    samples = terraingen.sample_truncated_power_law(
        terraingen.RandomStream(5, 5), 1.0, 100.0, 1.8, count=20000
    )
    assert samples.min() >= 1.0 and samples.max() <= 100.0
    low, high = 1.0, 100.0**-1.8
    d = ks_statistic(samples, lambda x: (low - x**-1.8) / (low - high))
    assert d < 1.949 / math.sqrt(len(samples))


def test_tabulated_distribution_cdf_interpolates_and_clamps():
    table = terraingen.TabulatedDistribution([0.0, 1.0, 5.0], [0.0, 0.25, 1.0])
    assert table.cdf(-1.0) == 0.0
    assert table.cdf(0.5) == pytest.approx(0.125)
    assert table.cdf(10.0) == 1.0


def test_tabulated_distribution_samples_match_its_cdf():
    table = terraingen.TabulatedDistribution([0.0, 1.0, 5.0], [0.0, 0.25, 1.0])
    samples = table.sample(terraingen.RandomStream(6, 6), count=20000)
    assert samples.min() >= 0.0 and samples.max() <= 5.0
    assert ks_statistic(samples, table.cdf) < 1.949 / math.sqrt(len(samples))
    assert 0.0 <= table.sample(terraingen.RandomStream(6, 7)) <= 5.0

# SPDX-License-Identifier: BSD-3-Clause

"""random: Philox streams and samplers."""

import math

import numpy as np
import pytest

import terraingen


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
        assert 0.0 <= stream.next_double() < 1.0


def test_substreams_differ():
    first = terraingen.RandomStream(1, 2, 0, 0, 0).u32s(4)
    assert list(first) != list(terraingen.RandomStream(1, 2, 1, 0, 0).u32s(4))


def test_samplers_return_scalars_or_arrays():
    stream = terraingen.RandomStream(2, 2)
    assert isinstance(terraingen.sample_poisson(stream, 3.0), int)
    counts = terraingen.sample_poisson(stream, 3.0, count=5)
    assert counts.shape == (5,) and counts.dtype == np.uint64


def test_samplers_reject_invalid_arguments():
    with pytest.raises(ValueError):
        terraingen.sample_poisson(terraingen.RandomStream(3, 3), -1.0)


@pytest.mark.parametrize("mean", [0.5, 4.0, 25.0, 300.0])
def test_poisson_mean_and_variance(mean):
    counts = terraingen.sample_poisson(terraingen.RandomStream(4, int(mean * 10)), mean, count=40000)
    counts = counts.astype(np.float64)
    assert counts.mean() == pytest.approx(mean, abs=5 * math.sqrt(mean / 40000))
    assert counts.var(ddof=1) / mean == pytest.approx(1.0, abs=0.05)

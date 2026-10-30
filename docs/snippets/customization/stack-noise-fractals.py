# SPDX-License-Identifier: BSD-3-Clause
"""Stack Noise Fractals Into a Layer."""

import terraingen


def stack_noise_fractals():
    # --8<-- [start:stack_noise_fractals]
    hills = terraingen.Fractal(seed=1, wavelength=200.0, amplitude=6.0, octaves=4)
    ridges = terraingen.Fractal(
        seed=2, wavelength=60.0, amplitude=1.5, octaves=5, type=terraingen.FractalType.RIDGED
    )
    noise = terraingen.NoiseLayer([hills, ridges])  # added in order
    terrain = terraingen.Terrain(source=terraingen.FlatSource(257, 257, 1.0), layers=[noise])
    heights = terrain.evaluate()
    # --8<-- [end:stack_noise_fractals]
    return heights


def test_stack_noise_fractals():
    values = stack_noise_fractals().values
    assert values.max() - values.min() > 1.0


def figure(quick=False):
    """Broad fBm hills with finer ridged noise on top, hillshaded."""
    import gallery
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(4.5, 4.5), layout="constrained")
    gallery.show_hillshade(ax, stack_noise_fractals().values, 1.0)
    return fig

# SPDX-License-Identifier: BSD-3-Clause
"""Brighten the Blast Zone Around a Lander."""

import terraingen

PIXEL = 1.0


def brighten_a_lander_blast_zone():
    # --8<-- [start:brighten_a_lander_blast_zone]
    zone = terraingen.BlastZone(x=0.0, y=0.0, area=28785.0, core_radius=15.0, seed=1)  # meters
    blasts = terraingen.BlastZoneAlbedo([zone])
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [blasts])
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (400.0, 400.0), PIXEL)  # meters
    (bands,) = terraingen.evaluate_albedo(albedo, grid)
    # --8<-- [end:brighten_a_lander_blast_zone]
    return bands.values


def test_brighten_a_lander_blast_zone():
    values = brighten_a_lander_blast_zone()
    assert values.shape == (400, 400)
    assert values[200, 200] < 0.11, "the dark core under the lander"
    assert values[200, 250] > 0.11, "the bright zone around it"
    assert abs(values[0, 0] - 0.11) < 1e-6, "undisturbed ground"


def figure(quick=False):
    """An Apollo-sized blast zone, bright around a dark core where the exhaust dug in."""
    import gallery
    import matplotlib.pyplot as plt

    values = brighten_a_lander_blast_zone()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    image = ax.imshow(values, cmap="gray", vmin=0.08, vmax=0.14, extent=gallery.extent(values, PIXEL))
    ax.grid(False)
    fig.colorbar(image, ax=ax, label="Albedo", shrink=0.85)
    return fig

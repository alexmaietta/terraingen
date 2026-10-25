# SPDX-License-Identifier: BSD-3-Clause
"""An Albedo With Bright Fresh Craters for the Quickstart."""

import terraingen


def add_albedo():
    source = terraingen.FlatSource(257, 257, 0.5)
    craters = terraingen.CraterLayer()
    # --8<-- [start:add_albedo]
    base = terraingen.SyntheticAlbedo(value=0.11)  # mature mare soil at 750 nm
    albedo = terraingen.Albedo(base, [terraingen.CraterAlbedo(craters)])
    (reflectance,) = terraingen.evaluate_albedo(albedo, source.grid())  # one band
    # --8<-- [end:add_albedo]
    return reflectance


def test_add_albedo():
    assert add_albedo().values.max() > 0.11


def figure(quick=False):
    """In the albedo, fresh craters and their ejecta are brighter than the mature soil around them."""
    import gallery
    import matplotlib.pyplot as plt

    values = add_albedo().values
    fig, ax = plt.subplots(figsize=(6, 5), layout="constrained")
    image = ax.imshow(values, cmap="gray", vmin=0.08, vmax=0.16, extent=gallery.extent(values, 0.5))
    ax.grid(False)
    fig.colorbar(image, ax=ax, label="Albedo", shrink=0.85)
    return fig

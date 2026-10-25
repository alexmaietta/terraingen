# SPDX-License-Identifier: BSD-3-Clause

"""Layered bedrock, with rolling hills on a 400 m square cut into beds about 4 m thick, each bed's top
weathering back into a cliff, so slopes step like mesas. From left to right the panels show the bare
hills, level beds, beds dipping 10 degrees to the east, and every other bed soft, weathering to a
smooth slope between the hard cliffs. Hillshaded with a low sun."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Bedrock and sand"

SIZE_M = 400.0
PANELS = [
    ("Bare hills", None),
    ("Level beds", {}),
    ("Dipping 10 degrees east", {"dip_deg": 10.0, "dip_azimuth_deg": 0.0}),
    ("Hard and soft beds", {"hard_share": 0.5}),
]


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    hills = terraingen.NoiseLayer(seed=5, wavelength=300.0, amplitude=40.0, octaves=5)

    fig, axes = plt.subplots(1, len(PANELS), figsize=(18, 4.8), layout="constrained")
    for ax, (title, fields) in zip(axes, PANELS, strict=True):
        layers = [hills]
        if fields is not None:
            layers.append(
                terraingen.StrataLayer(terraingen.StrataOptions(bed_thickness=4.0, seed=2, **fields))
            )
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), layers)
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, pixel, sun_elevation=25.0)
        ax.set_title(title)
    return fig

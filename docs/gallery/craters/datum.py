# SPDX-License-Identifier: BSD-3-Clause

"""What a crater is cut relative to, shown with an 80 m crater on ground rising 8 degrees to the
east with rolling relief. With no datum it is added to the ground, keeping the ground's bumps inside
it. The others replace the ground inside it with a crater cut into a datum fitted over one radius. A
plane tilts the smooth crater with the slope, while the mean or center height cuts it level, its
downslope rim standing high. The top row shows hillshades. The bottom row shows height above the 8
degree plane, so the tilt drops out and each datum's cut shows."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Slopes and datums"

DIAMETER = 80.0
SIZE_M = 240.0
TILT = np.tan(np.radians(8.0))
DATUMS = [
    ("No datum", terraingen.CraterDatum.NONE),
    ("Plane", terraingen.CraterDatum.PLANE),
    ("Mean height", terraingen.CraterDatum.MEAN_HEIGHT),
    ("Center height", terraingen.CraterDatum.CENTER_HEIGHT),
]


def figure(quick=False):
    pixel = 2.0 if quick else 0.4
    size = int(SIZE_M / pixel) + 1
    tilt = terraingen.FunctionLayer(lambda x, y, spacing, height: height + TILT * x)
    relief = terraingen.NoiseLayer(seed=3, wavelength=120.0, amplitude=4.0, octaves=5)
    x = (np.arange(size) - 0.5 * (size - 1)) * pixel
    plane = TILT * x[np.newaxis, :]

    fig, axes = plt.subplots(2, len(DATUMS), figsize=(15, 7.6), layout="constrained")
    for column, (name, datum) in enumerate(DATUMS):
        apply = terraingen.CraterApplyOptions()
        apply.datum = datum
        apply.interior_replacement = 1.0
        crater = terraingen.CraterLayer.manual(
            [terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)], apply=apply
        )
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(size, size, pixel), [tilt, relief, crater]
        )
        heights = terraingen.evaluate(terrain).values
        gallery.show_hillshade(axes[0, column], heights, pixel)
        axes[0, column].set_title(name)
        image = axes[1, column].imshow(
            heights - plane, cmap="RdBu_r", vmin=-12.0, vmax=12.0, extent=gallery.extent(heights, pixel)
        )
        axes[1, column].set_axis_off()
    fig.colorbar(image, ax=axes[1], label="Height above the tilted plane (m)", shrink=0.85)
    return fig

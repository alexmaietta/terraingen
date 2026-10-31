# SPDX-License-Identifier: BSD-3-Clause

"""Point queries as a physics engine makes them. A rover's four wheels, 2 m by 1.6 m apart, drive a
40 m path across cratered, bouldery ground, each wheel's height asked of the terrain at 5 cm with
height_at, no grid evaluated. The left panel shows the path over the hillshade. The right one shows
the wheel heights and the body's pitch and roll along the path."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

SPACING = 0.05
HALF_SIZE_M = 25.0
WHEELS = [(1.0, 0.8), (1.0, -0.8), (-1.0, 0.8), (-1.0, -0.8)]
STEPS = 400


def figure(quick=False):
    steps = 100 if quick else STEPS
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    craters = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=4, generation=terraingen.CraterGenerationOptions(min_diameter=2.5, max_diameter=12.0)
        )
    )
    boulders = terraingen.BoulderLayer(terraingen.BoulderLayerOptions(seed=4), craters)
    pixel = 0.2
    size = int(2.0 * HALF_SIZE_M / pixel) + 1
    terrain = terraingen.Terrain(
        source=terraingen.FlatSource(size, size, pixel), layers=[relief, craters, boulders]
    )

    travel = np.linspace(0.0, 1.0, steps)
    xs = -20.0 + 40.0 * travel
    ys = 4.0 * np.sin(2.0 * np.pi * travel)
    headings = np.arctan2(np.gradient(ys), np.gradient(xs))
    wheels = np.zeros((steps, len(WHEELS)))
    for step, (x, y, heading) in enumerate(zip(xs, ys, headings, strict=True)):
        cos, sin = np.cos(heading), np.sin(heading)
        for wheel, (forward, left) in enumerate(WHEELS):
            wheels[step, wheel] = terrain.height_at(
                x + forward * cos - left * sin, y + forward * sin + left * cos, SPACING
            )
    pitch = np.degrees(np.arctan2(wheels[:, :2].mean(axis=1) - wheels[:, 2:].mean(axis=1), 2.0))
    roll = np.degrees(np.arctan2(wheels[:, [0, 2]].mean(axis=1) - wheels[:, [1, 3]].mean(axis=1), 1.6))

    fig, (map_ax, plot_ax) = plt.subplots(
        1, 2, figsize=(16, 5.6), layout="constrained", width_ratios=[1, 1.4]
    )
    heights = terrain.evaluate().values
    map_ax.imshow(
        gallery.LIGHT.hillshade(heights, dx=pixel, dy=pixel),
        cmap="gray",
        vmin=0,
        vmax=1,
        extent=gallery.extent(heights, pixel),
    )
    map_ax.plot(xs, ys, color="#d62728", lw=2.0)
    map_ax.set_axis_off()
    map_ax.set_title("Path")
    distance = 40.0 * travel
    for wheel, name in enumerate(["Front left", "Front right", "Rear left", "Rear right"]):
        plot_ax.plot(distance, wheels[:, wheel] - wheels[:, wheel].mean(), lw=1.0, label=name)
    plot_ax.set_xlabel("Distance along the path (m)")
    plot_ax.set_ylabel("Wheel height about its mean (m)")
    twin = plot_ax.twinx()
    twin.plot(distance, pitch, color="#333333", lw=1.4, ls="--", label="Pitch")
    twin.plot(distance, roll, color="#888888", lw=1.4, ls=":", label="Roll")
    twin.set_ylabel("Body angle (degrees)")
    twin.grid(False)
    lines, labels = plot_ax.get_legend_handles_labels()
    more, more_labels = twin.get_legend_handles_labels()
    plot_ax.legend(lines + more, labels + more_labels, fontsize=8, ncol=3, loc="upper left")
    return fig

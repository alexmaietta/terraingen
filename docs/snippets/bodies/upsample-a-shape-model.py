# SPDX-License-Identifier: BSD-3-Clause
"""Upsample a Shape Model."""

import numpy as np

import terraingen


def radius(u):
    """A lumpy radius in meters along unit direction `u`."""
    return 220.0 + 80.0 * u[0] ** 2 + 12.0 * np.sin(9.0 * u[1]) + 8.0 * np.cos(7.0 * u[2])


def lumpy_body():
    """A lumpy body about 600 m long with 16 label intervals per cube face edge, about 15 m apart."""
    return terraingen.Surface.shape_model(terraingen.RadialShape.sample(16, radius))


def upsample_a_shape_model():
    body = lumpy_body()
    # --8<-- [start:upsample_a_shape_model]
    cubic = terraingen.ShapeModelSource(body)  # cubic between the labels by default
    lanczos = terraingen.ShapeModelSource(
        body, terraingen.KernelUpsampler(terraingen.ResampleMethod.LANCZOS3)
    )
    # Diamond-square with a roughness fitted to the labels.
    rough = terraingen.ShapeModelSource(body, terraingen.DiamondSquareUpsampler())
    terrain = terraingen.Terrain(body, rough)
    site = terraingen.RasterGrid.covering((20.0, 10.0, 30.0, 20.0), 0.05)  # longitude and latitude, degrees
    heights = terrain.evaluate(site, spacing=0.25)  # meters above the bilinear shape
    # --8<-- [end:upsample_a_shape_model]
    return body, {"Cubic": cubic, "Lanczos": lanczos, "Diamond-square": rough}, heights


def test_upsample_a_shape_model():
    body, sources, heights = upsample_a_shape_model()
    for source in sources.values():
        assert abs(source.height_at(0.0, 0.0, 1.0)) < 1e-3  # 0 on a label
    assert abs(sources["Cubic"].height_at(17.0, 23.0, 0.1)) > 1e-3
    assert heights.values.shape == (200, 200)
    assert heights.values.std() > 0.0


def figure(quick=False):
    """Heights above the bilinear shape along the equator from 20 E to 70 E, across the cube face
    edge at 45 E (dashed): what each upsampler adds between the labels (dots, 0 on each), against
    the smooth function the labels were sampled from."""
    import matplotlib.pyplot as plt

    body, sources, _ = upsample_a_shape_model()
    longitudes = np.linspace(20.0, 70.0, 400 if quick else 2000)
    shape = np.array([body.radius_at((lon, 0.0)) for lon in longitudes])
    truth = radius([np.cos(np.radians(longitudes)), np.sin(np.radians(longitudes)), 0.0])
    fig, ax = plt.subplots(figsize=(7.5, 4.0), layout="constrained")
    ax.plot(longitudes, truth - shape, color="#888888", lw=2.5, alpha=0.6, label="Sampled function")
    for name, source in sources.items():
        ax.plot(longitudes, [source.height_at(lon, 0.0, 0.01) for lon in longitudes], lw=1.0, label=name)
    labels = [
        np.degrees(np.arctan2(*terraingen.RadialShape.label_direction(16, i, 8, face)[1::-1]))
        for face in (5, 4)
        for i in range(17)
    ]
    labels = [lon for lon in labels if 20.0 <= lon <= 70.0]
    ax.plot(labels, np.zeros(len(labels)), "k.", ms=5, label="Labels")
    ax.axvline(45.0, color="#555555", lw=0.8, ls="--")
    ax.set_xlabel("Longitude on the equator (degrees east)")
    ax.set_ylabel("Height above the bilinear shape (m)")
    ax.legend(fontsize=8)
    return fig

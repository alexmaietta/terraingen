"""Boulder shapes: the default styles, a style of your own, and shapes shared by many boulders.

Boulders placed as 3D models get shapes from `BoulderShapeOptions`: each style starts from a solid
(cube, icosahedron, pyramid, prism, tetrahedron), is stretched to its axis ratios, rounded toward
an ellipsoid, roughened by noise and cut by fracture planes. A shape is a pure function of its seed,
so any boulder's mesh can be made again from its seed. This example writes one shape of each default
style and of a custom "egg" style as glTF, prints their axes, and shows top views of them.

    python example_boulders_shapes.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import montage, output_dir, write_png

SIZE = 96


def top_view(mesh, light=(-0.5, 0.5, 0.7)):
    """A shaded view of a shape from above: each pixel takes the highest triangle over it, lit
    from the north-west, on a white background."""
    positions, triangles = mesh.arrays
    light = np.asarray(light) / np.linalg.norm(light)
    depth = np.full((SIZE, SIZE), -np.inf)
    image = np.ones((SIZE, SIZE))
    px = (positions[:, 0] + 0.6) / 1.2 * (SIZE - 1)
    py = (0.6 - positions[:, 1]) / 1.2 * (SIZE - 1)
    for a, b, c in triangles:
        normal = np.cross(positions[b] - positions[a], positions[c] - positions[a])
        if normal[2] <= 0:
            continue
        shade = 0.15 + 0.85 * max(0.0, float(np.dot(normal / np.linalg.norm(normal), light)))
        xs, ys = px[[a, b, c]], py[[a, b, c]]
        cols = np.arange(int(np.floor(xs.min())), int(np.ceil(xs.max())) + 1)
        rows = np.arange(int(np.floor(ys.min())), int(np.ceil(ys.max())) + 1)
        gx, gy = np.meshgrid(cols, rows)
        area = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
        if area == 0:
            continue
        w1 = ((gx - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (gy - ys[0])) / area
        w2 = ((xs[1] - xs[0]) * (gy - ys[0]) - (gx - xs[0]) * (ys[1] - ys[0])) / area
        inside = (w1 >= 0) & (w2 >= 0) & (w1 + w2 <= 1) & (gx >= 0) & (gx < SIZE) & (gy >= 0) & (gy < SIZE)
        z = positions[a, 2] + w1 * (positions[b, 2] - positions[a, 2]) + w2 * (positions[c, 2] - positions[a, 2])
        for row, col, height in zip(gy[inside], gx[inside], z[inside]):
            if height > depth[row, col]:
                depth[row, col] = height
                image[row, col] = shade
    return image


def main():
    out = output_dir()
    egg = terraingen.BoulderStyle(
        name="egg",
        solid=terraingen.BaseSolid.ICOSAHEDRON,
        intermediate=terraingen.ValueRange(0.75, 0.8),
        short_axis=terraingen.ValueRange(0.6, 0.65),
        rounding=terraingen.ValueRange(1.0, 1.0),
        noise_amplitude=0.01,
    )
    styles = terraingen.default_boulder_styles() + [egg]
    views = []
    for index, style in enumerate(styles):
        options = terraingen.BoulderShapeOptions(styles=[style], subdivisions=3, seed=1)
        shape = terraingen.generate_boulder_shape(index + 1, options)
        print(
            f"{style.name:8s} ({style.solid.name.lower():11s}) axes 1 : {shape.intermediate:.2f} : {shape.short_axis:.2f}, "
            f"{shape.mesh.triangle_count} triangles"
        )
        shape.mesh.write_glb(out / f"example_boulders_shapes_{style.name}.glb")
        views.append(top_view(shape.mesh))
    print("ten boulders sharing 4 prototype shapes use seeds", sorted({terraingen.boulder_shape_seed(i, 4) for i in range(10)}))
    print("wrote", write_png(out / "example_boulders_shapes.png", montage(views, columns=7)))


if __name__ == "__main__":
    main()

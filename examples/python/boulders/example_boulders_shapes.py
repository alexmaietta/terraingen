"""Boulder shapes: the default styles, a style of your own, and shapes shared by many boulders.

Boulders placed as 3D models get shapes from `BoulderShapeOptions`: each style starts from a solid
(cube, icosahedron, pyramid, prism, tetrahedron), is stretched to its axis ratios, rounded toward
an ellipsoid, roughened by noise and cut by fracture planes. A shape is a pure function of its seed,
so any boulder's mesh can be made again from its seed. This example writes one shape of each default
style and of a custom "egg" style as glTF, prints their axes, and shows top views of them.

    python example_boulders_shapes.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import montage, write_png

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
        z = (
            positions[a, 2]
            + w1 * (positions[b, 2] - positions[a, 2])
            + w2 * (positions[c, 2] - positions[a, 2])
        )
        for row, col, height in zip(gy[inside], gx[inside], z[inside], strict=True):
            if height > depth[row, col]:
                depth[row, col] = height
                image[row, col] = shade
    return image


class Example(_common.Example):
    def generate(self):
        egg = terraingen.BoulderStyle(
            name="egg",
            solid=terraingen.BaseSolid.ICOSAHEDRON,
            intermediate=terraingen.ValueRange(0.75, 0.8),
            short_axis=terraingen.ValueRange(0.6, 0.65),
            rounding=terraingen.ValueRange(1.0, 1.0),
            noise_amplitude=0.01,
        )
        self.styles = terraingen.default_boulder_styles() + [egg]
        self.shapes = []
        for index, style in enumerate(self.styles):
            options = terraingen.BoulderShapeOptions(styles=[style], subdivisions=3, seed=1)
            shape = terraingen.generate_boulder_shape(index + 1, options)
            print(
                f"{style.name:8s} ({style.solid.name.lower():11s}) axes 1 : {shape.intermediate:.2f} : {shape.short_axis:.2f}, "
                f"{shape.mesh.triangle_count} triangles"
            )
            self.shapes.append(shape)
        self.seeds = sorted({terraingen.boulder_shape_seed(i, 4) for i in range(10)})
        print("ten boulders sharing 4 prototype shapes use seeds", self.seeds)

    def save(self, out):
        for style, shape in zip(self.styles, self.shapes, strict=True):
            shape.mesh.write_glb(out / f"example_boulders_shapes_{style.name}.glb")
        views = [top_view(shape.mesh) for shape in self.shapes]
        print("wrote", write_png(out / "example_boulders_shapes.png", montage(views, columns=7)))

    def test_final(self):
        assert len(self.shapes) == len(self.styles) >= 2, "one shape for each style"
        assert all(shape.mesh.triangle_count > 0 for shape in self.shapes), "every shape has triangles"
        assert all(0.0 < s.short_axis <= s.intermediate <= 1.0 for s in self.shapes), (
            "axes run long >= intermediate >= short > 0"
        )
        egg = self.shapes[-1]
        assert 0.7 < egg.intermediate < 0.85 and 0.55 < egg.short_axis < 0.7, (
            "the egg keeps near its axis ratios"
        )
        assert len(self.seeds) == 4, "ten boulders sharing 4 prototypes use 4 shapes"


if __name__ == "__main__":
    _common.main(Example)

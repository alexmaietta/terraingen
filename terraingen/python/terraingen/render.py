"""Render TerrainGen rasters with Ernie, TerrainGen's renderer.

These helpers need the ``ernie`` Python module, built from an Ernie checkout. They put a raster
into an Ernie scene without copying its cells (Ernie reads them through DLPack and keeps the raster
alive), and add a sun and a camera looking at it, so a script can generate and render headlessly::

    import ernie
    import terraingen
    from terraingen import render

    raster = terraingen.Raster(...)
    terraingen.SyntheticSource(...).fill(raster)
    scene = ernie.Scene()
    render.add_terrain(scene, raster)
    render.add_sun(scene, azimuth_deg=315.0, elevation_deg=20.0)
    render.frame_terrain(scene, raster)
    image = ernie.Renderer().render(scene)

Positions follow TerrainGen: x east, y north, z up. Sun azimuths here are compass bearings,
clockwise from north, unlike the angles of TerrainGen's configurations and options, which are
counter-clockwise from +x (east).
"""

import math

import numpy as np

from . import _terraingen

TERRAIN_PATH = "/World/Terrain"
SUN_PATH = "/World/Sun"
CAMERA_PATH = "/World/Camera"
BOULDERS_PATH = "/World/Boulders"


def _ernie():
    try:
        import ernie
    except ImportError as error:
        raise ImportError("terraingen.render needs the ernie module, built from an Ernie checkout") from error
    return ernie


def _check_north_up(raster):
    t = raster.transform
    if t.row_rotation != 0.0 or t.col_rotation != 0.0 or not t.pixel_width > 0.0:
        raise ValueError("Ernie renders north-up rasters without rotation")
    if t.pixel_height != -t.pixel_width:
        raise ValueError("Ernie renders rasters with square pixels")
    return t


def direction(azimuth_deg, elevation_deg):
    """Unit vector toward azimuth (clockwise from north) and elevation above the horizon."""
    azimuth = math.radians(azimuth_deg)
    elevation = math.radians(elevation_deg)
    return (
        math.sin(azimuth) * math.cos(elevation),
        math.cos(azimuth) * math.cos(elevation),
        math.sin(elevation),
    )


def terrain_bounds(raster, exaggeration=1.0):
    """World bounds ``((x0, y0, z0), (x1, y1, z1))`` of a raster's cell centers and valid heights."""
    t = _check_north_up(raster)
    stats = _terraingen.compute_stats(raster)
    low, high = (stats.min, stats.max) if stats.valid_count > 0 else (0.0, 0.0)
    min_x, min_y, max_x, max_y = _terraingen.RasterGrid.from_raster(raster).bounds
    half = 0.5 * t.pixel_width
    return (min_x + half, min_y + half, low * exaggeration), (max_x - half, max_y - half, high * exaggeration)


def add_terrain(scene, raster, path=TERRAIN_PATH, *, exaggeration=None, material=None):
    """Defines a Terrain prim whose heights are ``raster``'s cells, shared without copying.

    The raster's geotransform becomes the prim's origin and cell size, and its nodata value the
    prim's nodata. ``material`` is the path of a Material prim to bind. Writing the raster again
    (for example after regenerating it in place) tells Ernie to upload it again. Raises ValueError
    for rotated rasters or rectangular pixels, which Ernie does not render.
    """
    ernie = _ernie()
    tokens = ernie.tokens
    t = _check_north_up(raster)
    prim = scene.define(path, tokens.TERRAIN)
    prim.write(tokens.HEIGHTS, raster)
    prim.set(tokens.ORIGIN, (float(t.origin_x), float(t.origin_y)))
    prim.set(tokens.CELL_SIZE, float(t.pixel_width))
    if raster.no_data is not None:
        prim.set(tokens.NO_DATA, float(raster.no_data))
    else:
        scene.clear(path, tokens.NO_DATA)
    if exaggeration is not None:
        prim.set(tokens.EXAGGERATION, float(exaggeration))
    if material is not None:
        prim.set(tokens.MATERIAL_BINDING, ernie.Path(str(material)))
    return prim


def _quaternion(rotation):
    """The unit quaternion (x, y, z, w) of a row-major 3 x 3 rotation matrix."""
    m = np.asarray(rotation, dtype=np.float64).reshape(3, 3)
    trace = m[0, 0] + m[1, 1] + m[2, 2]
    if trace > 0.0:
        s = 2.0 * math.sqrt(trace + 1.0)
        q = ((m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s)
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = 2.0 * math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2])
        q = (0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s)
    elif m[1, 1] > m[2, 2]:
        s = 2.0 * math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2])
        q = ((m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s)
    else:
        s = 2.0 * math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1])
        q = ((m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s)
    return np.asarray(q) / np.linalg.norm(q)


def _define_mesh(scene, path, positions, triangles, normals, material):
    ernie = _ernie()
    tokens = ernie.tokens
    prim = scene.define(
        path,
        ernie.Mesh,
        points=np.ascontiguousarray(positions, dtype=np.float32),
        normals=np.ascontiguousarray(normals, dtype=np.float32),
        face_vertex_counts=np.full(len(triangles), 3, dtype=np.int32),
        face_vertex_indices=np.ascontiguousarray(np.asarray(triangles).reshape(-1), dtype=np.int32),
    )
    if material is not None:
        prim.set(tokens.MATERIAL_BINDING, ernie.Path(str(material)))
    return prim


def _shaded(shape, crease_angle_deg):
    """(mesh, normals) of a boulder shape split along its creases."""
    return shape.mesh.with_creases(crease_angle_deg)


def add_boulders(scene, models, path=BOULDERS_PATH, *, instanced=True, crease_angle_deg=60.0, material=None):
    """Defines the boulder models (``BoulderModel``, as ``placed_boulders`` gives) as 3D meshes.

    Instanced, each shape the boulders share becomes one prototype Mesh under a PointInstancer
    that draws it once per boulder, scaled, turned and placed; otherwise every boulder becomes its
    own Mesh with its placed vertices. Normals are split where faces meet at more than
    ``crease_angle_deg`` and smooth elsewhere. Planar terrains only: on a body, placements are in
    each boulder's local frame. ``material`` is the path of a Material prim to bind. Returns the
    prim.
    """
    ernie = _ernie()
    tokens = ernie.tokens
    base = ernie.Path(str(path))
    if not instanced:
        scene.define(base, tokens.XFORM)
        for n, model in enumerate(models):
            mesh, normals = _shaded(model.shape, crease_angle_deg)
            placed, triangles = model.placement.place_mesh(mesh).arrays
            rotation = np.asarray(model.placement.pose.rotation).reshape(3, 3)
            turned = (normals / np.asarray(model.placement.scale)) @ rotation.T
            turned /= np.linalg.norm(turned, axis=1, keepdims=True)
            _define_mesh(scene, base.child(f"Boulder{n}"), placed, triangles, turned, material)
        return scene.get_prim(base)
    shapes = {}
    indices = []
    for model in models:
        key = (model.shape_seed, model.boulder.parent, model.boulder.piece)
        indices.append(shapes.setdefault(key, (len(shapes), model.shape))[0])
    prototypes = []
    for index, shape in shapes.values():
        prototype = base.child(f"Shape{index}")
        mesh, normals = _shaded(shape, crease_angle_deg)
        _define_mesh(scene, prototype, *mesh.arrays, normals, material)
        prototypes.append(prototype)
    count = len(indices)
    positions = np.zeros((count, 3), np.float32)
    orientations = np.zeros((count, 4), np.float32)
    scales = np.zeros((count, 3), np.float32)
    for n, model in enumerate(models):
        pose = model.placement.pose
        positions[n] = pose.translation
        orientations[n] = _quaternion(pose.rotation)
        scales[n] = model.placement.scale
    return scene.define(
        base,
        ernie.PointInstancer,
        positions=positions,
        orientations=orientations,
        scales=scales,
        proto_indices=np.asarray(indices, np.int32),
        prototypes=prototypes,
    )


def add_sun(scene, path=SUN_PATH, *, azimuth_deg=315.0, elevation_deg=35.0, intensity=1.0):
    """Defines a DistantLight shining from the given azimuth and elevation."""
    ernie = _ernie()
    tokens = ernie.tokens
    toward = direction(azimuth_deg, elevation_deg)
    return (
        scene.define(path, tokens.DISTANT_LIGHT)
        .set(tokens.TRANSFORM, ernie.distant_light_transform(toward))
        .set(tokens.INTENSITY, float(intensity))
    )


def frame_terrain(
    scene,
    raster,
    path=CAMERA_PATH,
    *,
    azimuth_deg=180.0,
    elevation_deg=45.0,
    vertical_fov_deg=40.0,
    exaggeration=1.0,
    margin=1.1,
):
    """Defines a Camera that looks at the raster's center from the given azimuth and elevation.

    The camera stands far enough away for the terrain's bounding sphere, times ``margin``, to fit
    its vertical field of view, keeping USD's 50 mm focal length and fitting the vertical aperture
    to the field of view.
    """
    ernie = _ernie()
    tokens = ernie.tokens
    low, high = terrain_bounds(raster, exaggeration)
    center = tuple(0.5 * (a + b) for a, b in zip(low, high, strict=True))
    radius = 0.5 * math.dist(low, high)
    half_fov = math.radians(vertical_fov_deg) / 2.0
    distance = max(radius * margin / math.sin(half_fov), 1e-3)
    back = direction(azimuth_deg, elevation_deg)
    eye = tuple(c + distance * d for c, d in zip(center, back, strict=True))
    up = (0.0, 0.0, 1.0) if abs(back[2]) < 0.999 else (0.0, 1.0, 0.0)
    focal = 50.0
    return (
        scene.define(path, tokens.CAMERA)
        .set(tokens.TRANSFORM, ernie.look_at_transform(eye, center, up))
        .set(tokens.FOCAL_LENGTH, focal)
        .set(tokens.VERTICAL_APERTURE, 2.0 * focal * math.tan(half_fov))
        .set(tokens.CLIPPING_RANGE, (max(distance - 2 * radius, distance * 1e-3), distance + 2 * radius))
    )

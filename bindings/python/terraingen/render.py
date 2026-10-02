"""Render TerrainGen rasters with Ernie, TerrainGen's renderer.

These helpers need the ``ernie`` Python module, built with TerrainGen's viewer. They put a raster
into an Ernie scene without copying its cells (Ernie reads them through DLPack and keeps the raster
alive), and add a sun and a camera looking at it, so a script can generate and render headlessly::

    import ernie
    import terraingen as tg
    from terraingen import render

    raster = tg.generate_synthetic_terrain(...)
    scene = ernie.Scene()
    render.add_terrain(scene, raster)
    render.add_sun(scene, azimuth_deg=315.0, elevation_deg=20.0)
    render.frame_terrain(scene, raster)
    image = ernie.RasterRenderer().render(scene)

Directions follow TerrainGen and the viewer: x east, y north, z up, azimuths clockwise from north.
"""

import math

import numpy as np

TERRAIN_PATH = "/World/Terrain"
SUN_PATH = "/World/Sun"
CAMERA_PATH = "/World/Camera"


def _ernie():
    try:
        import ernie
    except ImportError as error:
        raise ImportError(
            "terraingen.render needs the ernie module, built with TerrainGen's viewer"
        ) from error
    return ernie


def _check_north_up(raster):
    t = raster.transform
    if t.row_rotation != 0.0 or t.col_rotation != 0.0 or not t.pixel_width > 0.0:
        raise ValueError("Ernie renders north-up rasters without rotation")
    if t.pixel_height != -t.pixel_width:
        raise ValueError("Ernie renders rasters with square pixels")
    return t


def direction(azimuth_deg, elevation_deg):
    """Unit vector towards azimuth (clockwise from north) and elevation above the horizon."""
    azimuth = math.radians(azimuth_deg)
    elevation = math.radians(elevation_deg)
    return (
        math.sin(azimuth) * math.cos(elevation),
        math.cos(azimuth) * math.cos(elevation),
        math.sin(elevation),
    )


def terrain_bounds(raster, exaggeration=1.0):
    """World bounds ``((x0, y0, z0), (x1, y1, z1))`` of a raster's cell centres and valid heights."""
    t = _check_north_up(raster)
    values = np.asarray(raster.values)
    valid = np.isfinite(values)
    if raster.nodata is not None:
        valid &= values != raster.nodata
    low = float(values[valid].min()) if valid.any() else 0.0
    high = float(values[valid].max()) if valid.any() else 0.0
    half = 0.5 * t.pixel_width
    x0 = t.origin_x + half
    x1 = t.origin_x + raster.width * t.pixel_width - half
    y1 = t.origin_y - half
    y0 = t.origin_y - raster.height * t.pixel_width + half
    return (x0, y0, low * exaggeration), (x1, y1, high * exaggeration)


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
    if raster.nodata is not None:
        prim.set(tokens.NO_DATA, float(raster.nodata))
    else:
        scene.clear(path, tokens.NO_DATA)
    if exaggeration is not None:
        prim.set(tokens.EXAGGERATION, float(exaggeration))
    if material is not None:
        prim.set(tokens.MATERIAL_BINDING, ernie.Path(str(material)))
    return prim


def add_sun(scene, path=SUN_PATH, *, azimuth_deg=315.0, elevation_deg=35.0, intensity=1.0):
    """Defines a DistantLight shining from the given azimuth and elevation."""
    ernie = _ernie()
    tokens = ernie.tokens
    towards = direction(azimuth_deg, elevation_deg)
    return (
        scene.define(path, tokens.DISTANT_LIGHT)
        .set(tokens.TRANSFORM, ernie.distant_light_transform(towards))
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
    """Defines a Camera that looks at the raster's centre from the given azimuth and elevation.

    The camera stands far enough away for the terrain's bounding sphere, times ``margin``, to fit
    its vertical field of view, keeping USD's 50 mm focal length and fitting the vertical aperture
    to the field of view.
    """
    ernie = _ernie()
    tokens = ernie.tokens
    low, high = terrain_bounds(raster, exaggeration)
    centre = tuple(0.5 * (a + b) for a, b in zip(low, high))
    radius = 0.5 * math.dist(low, high)
    half_fov = math.radians(vertical_fov_deg) / 2.0
    distance = max(radius * margin / math.sin(half_fov), 1e-3)
    back = direction(azimuth_deg, elevation_deg)
    eye = tuple(c + distance * d for c, d in zip(centre, back))
    up = (0.0, 0.0, 1.0) if abs(back[2]) < 0.999 else (0.0, 1.0, 0.0)
    focal = 50.0
    return (
        scene.define(path, tokens.CAMERA)
        .set(tokens.TRANSFORM, ernie.look_at_transform(eye, centre, up))
        .set(tokens.FOCAL_LENGTH, focal)
        .set(tokens.VERTICAL_APERTURE, 2.0 * focal * math.tan(half_fov))
        .set(tokens.CLIPPING_RANGE, (max(distance - 2 * radius, distance * 1e-3), distance + 2 * radius))
    )

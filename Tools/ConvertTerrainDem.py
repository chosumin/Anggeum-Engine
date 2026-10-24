"""Converts a DEM (one or more GeoTIFF tiles) into the terrain height map pair
ImageTerrainHeightSource reads: <name>.r16 (raw little-endian uint16,
row-major, row 0 at the world's min Z = north) and <name>.json (its size and
the height range in meters).

The world square (--extent meters, centered on --center or the mosaic
center) is resampled onto (--size)^2 texels whose first and last texel sit on
the world's edges, so 2048 m at --size 4097 puts a texel every 0.5 m, one per
LOD 0 vertex. Elevations are rebased so the world center reads 0 m (the
Sponza pad), unless --no-rebase.

Example:
  python Tools/ConvertTerrainDem.py tile_a.tif tile_b.tif
      --out Assets/Terrain/swiss_alps --extent 2048 --size 4097

Needs rasterio (pip install rasterio numpy).
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
import rasterio
from rasterio.crs import CRS
from rasterio.enums import Resampling
from rasterio.merge import merge
from rasterio.transform import Affine
from rasterio.warp import reproject, transform


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("inputs", nargs="+", help="GeoTIFF tile(s)")
    p.add_argument("--out", required=True, help="output path without extension")
    p.add_argument("--extent", type=float, default=2048.0, help="world edge in meters")
    p.add_argument("--size", type=int, default=4097, help="texels per edge")
    p.add_argument("--center", type=float, nargs=2, metavar=("LON", "LAT"),
                   help="world center in WGS84 (default: mosaic center)")
    p.add_argument("--no-rebase", action="store_true",
                   help="keep absolute elevations instead of zeroing the center")
    p.add_argument("--headroom", type=float, nargs="+", default=[0.0],
                   help="meters added below and above the range (one value for both): "
                        "room for detail noise, or to keep the biome height fractions low")
    p.add_argument("--attribution", default="",
                   help="data source and licence, kept in the descriptor")
    return p.parse_args()


def mosaic(paths):
    sources = [rasterio.open(p) for p in paths]
    data, src_transform = merge(sources, resampling=Resampling.bilinear)
    crs = sources[0].crs
    nodata = sources[0].nodata
    for s in sources:
        s.close()
    return data[0].astype(np.float64), src_transform, crs, nodata


def local_crs(crs, src_transform, shape, center_lonlat):
    if center_lonlat is None:
        cx = src_transform.c + src_transform.a * shape[1] / 2.0
        cy = src_transform.f + src_transform.e * shape[0] / 2.0
        lon, lat = transform(crs, CRS.from_epsg(4326), [cx], [cy])
        lon, lat = lon[0], lat[0]
    else:
        lon, lat = center_lonlat
    # A transverse Mercator centered on the world: meters, negligible
    # distortion over a few km, independent of the source projection.
    dst_crs = CRS.from_proj4(
        f"+proj=tmerc +lat_0={lat} +lon_0={lon} +k=1 +x_0=0 +y_0=0 +ellps=WGS84 +units=m +no_defs")
    return dst_crs, (lon, lat)


def main():
    args = parse_args()
    if args.size < 2:
        sys.exit("--size must be at least 2")

    src, src_transform, src_crs, nodata = mosaic(args.inputs)
    if nodata is not None:
        src[src == nodata] = np.nan

    dst_crs, (lon, lat) = local_crs(src_crs, src_transform, src.shape, args.center)

    # Texel i center at x = -extent/2 + i * step: the first and last texel
    # sit on the world edges. Row 0 is north (max northing), the world's -Z.
    step = args.extent / (args.size - 1)
    half = args.extent / 2.0
    dst_transform = Affine(step, 0.0, -half - step / 2.0,
                           0.0, -step, half + step / 2.0)
    dst = np.full((args.size, args.size), np.nan, dtype=np.float64)
    reproject(src, dst, src_transform=src_transform, src_crs=src_crs,
              src_nodata=np.nan, dst_transform=dst_transform, dst_crs=dst_crs,
              dst_nodata=np.nan, resampling=Resampling.bilinear)

    missing = np.isnan(dst)
    if missing.all():
        sys.exit("the world square does not overlap the DEM")
    if missing.any():
        print(f"warning: {missing.sum()} texels outside the DEM, filled with the mean")
        dst[missing] = np.nanmean(dst)

    offset = 0.0
    if not args.no_rebase:
        c = (args.size - 1) // 2
        offset = float(dst[c, c])
    dst -= offset

    below, above = args.headroom[0], args.headroom[-1]
    height_min = float(dst.min()) - below
    height_max = float(dst.max()) + above
    if height_max <= height_min:
        height_max = height_min + 1.0

    packed = np.round((dst - height_min) / (height_max - height_min) * 65535.0)
    packed = np.clip(packed, 0, 65535).astype("<u2")

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    packed.tofile(out.with_suffix(".r16"))
    descriptor = {
        "data": out.with_suffix(".r16").name,
        "width": args.size,
        "height": args.size,
        "heightMin": round(height_min, 3),
        "heightMax": round(height_max, 3),
        "extentMeters": args.extent,
        "centerLonLat": [lon, lat],
        "elevationOffset": round(offset, 3),
        "sources": [Path(p).name for p in args.inputs],
        "attribution": args.attribution,
    }
    out.with_suffix(".json").write_text(json.dumps(descriptor, indent=2) + "\n")
    print(f"wrote {out.with_suffix('.r16')} ({args.size}x{args.size}, "
          f"{packed.nbytes / 1e6:.1f} MB), range [{height_min:.1f}, {height_max:.1f}] m, "
          f"center ({lon:.5f}, {lat:.5f}) at {offset:.1f} m")


if __name__ == "__main__":
    main()

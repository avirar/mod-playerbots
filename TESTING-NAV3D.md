# Nav3D Uldis test build

This branch is the full 3D-nav/flight combo for testing the Uldis tunnel route.

## Branches

- Core: `combo/nav3d-uldis` on `avirar/azerothcore-wotlk`
- Module: `combo/nav3d-uldis` on `avirar/mod-playerbots`

## Build

1. Check out the core combo branch and build `worldserver`.
2. Put the module combo branch at `modules/mod-playerbots`.
3. In `env/dist/etc/modules/playerbots.conf`, set:

   ```ini
   Playerbots.Nav3D.Enabled = 1
   Playerbots.Nav3D.Lazy = 0
   Playerbots.Nav3D.CellSize = 1.0
   Playerbots.Nav3D.Neighbors = 26
   Playerbots.Nav3D.MaxNodes = 2000000
   Playerbots.Nav3D.MaxSearchTimeMs = 10000
   ```

   `MaxNodes` / `MaxSearchTimeMs` matter: the dist defaults (20000 / 50 ms)
   are too small for the Uldis detail search.

4. Install the bundled **1-yd** detail tile:

   ```bash
   mkdir -p <DataDir>/nav3d/map0571
   cp data/nav3d/map0571/18_33_airdetail.n3t <DataDir>/nav3d/map0571/
   ```

   `<DataDir>` is the worldserver data directory (for a normal build,
   `env/dist/bin`). `Playerbots.Nav3D.DataPath` defaults to `nav3d` and is
   relative to it.

   The 4-yd fallback is bundled as `18_33_airdetail_4yd.n3t`; copy it over
   `18_33_airdetail.n3t` to use the fallback.

## Read-only route check

From the worldserver console:

```text
.playerbots nav3dair 571 6937.7 -616.77 1013.75 7310.269 -715.8104 791.61053
```

Expected on the 1-yd tile:

```text
nav3d detail coarse map 571 found=1 points=14 ms=~1700 nodes=~585000
nav3d detail refined map 571 points=15 len=706.8
nav3dair 571 ... found=1 ... points=15 len=706.8 cell=1.0
```

The points run through the tunnel:

```text
entrance (7178,-474,958)
mid      (7286,-474,910)
lower    (7302,-550,838)
GO       (7310,-716,792)
```

If it instead dives south through `(7190,-710,910)`, the runtime is still using
a narrow search band or the old tile.

## Movement check

Put the test character at the bridge start, `6937.7 -616.77 1013.75`, on a
flying mount, then from the console:

```text
.playerbots nav3dmove <Character> fly 7310.269 -715.8104 791.61053
```

Expected: the flight enters the tunnel at the north mouth, follows the cave
south, and lands at the GO. It must not cut through the cliff.

## Regenerate the tile

The 1-yd tile is the production path; the same command with
`--cell-size 4.0` produces the fallback. From the core combo branch:

```bash
<build>/src/tools/nav3d_generator \
  --data-dir <DataDir> --output <out> --map 571 --tile 18:33 \
  --cell-size 1.0 --air-tree-detail \
  --box 6932 7380 -780 -420 700 1250 --jobs 8 \
  --seed 6937.7 -616.77 1013.75 \
  --seed 6937.7 -616.77 1033.75 \
  --seed 7177.835 -461.98758 961.76196 \
  --seed 7292.68 -459.16083 919.0399 \
  --seed 7297.8677 -522.32996 834.741 \
  --seed 7310.269 -715.8104 791.61053
```

The output is `<out>/map0571/18_33_airdetail.n3t`.

The 1-yd runtime uses a 4-yd coarse pass over the fine volume, then refines each
leg at the native cell size in a local box. That keeps the path inside the
tunnel while staying inside the 2M-node / 10-s budget.

#!/usr/bin/env python3
"""Generate src/tools/face-mask/facemesh-tables.hpp from MediaPipe's canonical_face_model.obj.

Emits the FaceMesh triangulation (898 tris) and a frontal-projection UV per
vertex (image mapped as a front-facing decal, so any picture/video works).

Usage:
    curl -fsSL \
      https://github.com/google-ai-edge/mediapipe/raw/master/mediapipe/modules/face_geometry/data/canonical_face_model.obj \
      -o /tmp/cfm.obj
    python3 build-aux/gen-facemesh-tables.py /tmp/cfm.obj src/tools/face-mask/facemesh-tables.hpp
    (then clang-format -i the output)
"""
import sys

obj = sys.argv[1] if len(sys.argv) > 1 else "/tmp/cfm.obj"
dst = sys.argv[2] if len(sys.argv) > 2 else "src/tools/face-mask/facemesh-tables.hpp"

V, F = [], []
for line in open(obj):
    if line.startswith("v "):
        _, x, y, z = line.split()[:4]
        V.append((float(x), float(y), float(z)))
    elif line.startswith("f "):
        idx = [int(tok.split("/")[0]) - 1 for tok in line.split()[1:]]
        assert len(idx) == 3, idx
        F.append(tuple(idx))
assert len(V) == 468 and len(F) == 898, (len(V), len(F))

xs = [v[0] for v in V]
ys = [v[1] for v in V]
xmin, xmax = min(xs), max(xs)
ymin, ymax = min(ys), max(ys)


def uv(v):
    u = (v[0] - xmin) / (xmax - xmin)
    w = (ymax - v[1]) / (ymax - ymin)  # flip Y: face top -> texture top
    return u, w


out = []
out.append("#pragma once")
out.append("")
out.append("// AUTO-GENERATED from MediaPipe canonical_face_model.obj (Apache-2.0).")
out.append("// 468 FaceMesh vertices: canonical triangulation + frontal-projection UV")
out.append("// (image is mapped as a front-facing decal, so any picture/video works).")
out.append("// Regenerate with build-aux/gen-facemesh-tables.py.")
out.append("#include <cstdint>")
out.append("")
out.append("namespace FaceMask::facemesh {")
out.append("inline constexpr int kNumVerts = 468;")
out.append("inline constexpr int kNumTris = 898;")
out.append("")
out.append("// Per-vertex texture coords in [0,1], (0,0) = top-left of the mask image.")
out.append("inline constexpr float kUV[kNumVerts][2] = {")
for u, w in (uv(v) for v in V):
    out.append(f"\t{{{u:.6f}f, {w:.6f}f}},")
out.append("};")
out.append("")
out.append("// Triangle list (0-based vertex indices into the 468 mesh points).")
out.append("inline constexpr uint16_t kTris[kNumTris][3] = {")
for a, b, c in F:
    out.append(f"\t{{{a}, {b}, {c}}},")
out.append("};")
out.append("} // namespace FaceMask::facemesh")
out.append("")
open(dst, "w").write("\n".join(out))
print(f"wrote {dst}  verts {len(V)} tris {len(F)}")

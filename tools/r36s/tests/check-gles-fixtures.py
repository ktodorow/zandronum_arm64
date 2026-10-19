#!/usr/bin/env python3
"""Check actual fixture bytes, voxel face colors and preserved V1 scene data.

Structural checks supplement the actual engine parser and visual reference gate.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile


V1_SHA256 = "ab485c476d4be67315730e9f7e1d4c3b14c717adf4931d7c901d94019041cae2"
V2_CHANGED_MEMBERS = {"maps/GLTEST03.wad", "voxels/fixture.kvx", "fixtures.json"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wad_lumps(data):
    require(data[:4] == b"PWAD", "fixture map must be PWAD")
    count, directory = struct.unpack_from("<II", data, 4)
    require(directory >= 12 and directory + count * 16 == len(data), "WAD directory bounds")
    result = {}
    for index in range(count):
        offset, size, name = struct.unpack_from("<II8s", data, directory + index * 16)
        require(12 <= offset <= directory and offset + size <= directory, "WAD lump bounds")
        result[name.rstrip(b"\0").decode()] = data[offset:offset + size]
    require(result.get("BEHAVIOR", b"")[:4] in (b"ACS\0", b"ACSe", b"ACSE"), "compiled ACS signature")
    require(result.get("TEXTMAP", b"").startswith(b'namespace = "ZDoom";'), "UDMF namespace")
    return result


def check_voxel(data):
    size, sx, sy, sz, px, py, pz = struct.unpack_from("<7i", data)
    require((sx, sy, sz) == (12, 12, 24), "KVX dimensions")
    require((px, py, pz) == (sx * 128, sy * 128, sz * 256), "KVX pivot")
    require(size + 4 + 768 == len(data), "KVX single mip/palette boundary")
    offset_size = (sx + 1) * 4 + sx * (sy + 1) * 2
    xoffset = struct.unpack_from("<" + "i" * (sx + 1), data, 28)
    xyoffset = struct.unpack_from("<" + "h" * (sx * (sy + 1)), data, 28 + (sx + 1) * 4)
    require(xoffset[0] == offset_size and xoffset[-1] == size - 24, "KVX X offset endpoints")
    palette = [tuple(data[-768 + i * 3: -768 + (i + 1) * 3]) for i in range(255)]
    palette.append(tuple(data[-3:]))
    require(all(0 <= channel < 64 for color in palette for channel in color), "six-bit KVX palette")
    used, face_indices = set(), set()
    for x in range(sx):
        for y in range(sy):
            start = 28 + xoffset[x] + xyoffset[x * (sy + 1) + y]
            end = 28 + xoffset[x] + xyoffset[x * (sy + 1) + y + 1]
            require(28 + offset_size <= start < end <= 4 + size, "KVX slab bounds")
            top, length, faces = data[start:start + 3]
            require((top, length, end - start) == (0, sz, sz + 3), "complete single slab per column")
            expected = 16 | 32 | (1 if x == 0 else 0) | (2 if x == sx - 1 else 0) | (4 if y == 0 else 0) | (8 if y == sy - 1 else 0)
            require(faces == expected, "KVX exterior face visibility mask")
            colors = data[start + 3:end]
            require(colors == bytes(32 + (z // 4 % 3) * 64 for z in range(sz)), "KVX horizontal four-voxel bands")
            used.update(colors)
            if faces & 15:
                face_indices.update(colors)
    require(used == face_indices == {32, 96, 160}, "all diagnostic colors appear on exterior sides")
    colors = [palette[index] for index in sorted(face_indices)]
    require(len(set(colors)) == 3, "KVX side colors must not alias in palette")
    require(all(max(color) - min(color) >= 50 for color in colors), "KVX side colors must be visibly saturated")
    require({color.index(max(color)) for color in colors} == {0, 1, 2}, "KVX red/green/blue channel distinction")
    return dict(dimensions=[sx, sy, sz], exterior_palette_6bit=dict(zip(sorted(face_indices), colors)))


def check(args):
    raw = args.fixture_pk3.read_bytes()
    with zipfile.ZipFile(args.fixture_pk3) as archive:
        content = {name: archive.read(name) for name in archive.namelist()}
    manifest = json.loads(content["fixtures.json"])
    require(manifest.get("fixture_version") == 2, "fixture version2 metadata")
    require(set(content) == set(manifest["assets"]) | {"fixtures.json"}, "manifest covers every archive asset")
    for name, digest in manifest["assets"].items():
        require(sha(content[name]) == digest, "asset checksum: " + name)
    require([item["map"] for item in manifest["maps"]] == [f"GLTEST{i:02d}" for i in range(1, 7)], "all six fixture maps")
    for item in manifest["maps"]:
        lumps = wad_lumps(content[f'maps/{item["map"]}.wad'])
        text = lumps["TEXTMAP"].decode()
        for kind, key in (("sector", "sectors"), ("linedef", "linedefs"), ("thing", "things")):
            require(len(re.findall(r"^" + kind + r" \{", text, re.M)) == item[key], "UDMF count: " + item["map"] + " " + kind)
        if item["map"] == "GLTEST03":
            require(item["camera"]["pitch_degrees"] == -33.75, "sky fixed upward pitch metadata")
            source = lumps["SCRIPTS"].decode()
            require(source.count("SetActorPitch(9002, -0.09375)") == 2, "ENTER and restore sky pitch scripts")
            require("GetActorPitch(9002)" in source and item["required_markers"] == ["GLES-FIXTURE SKY PITCH -6144"], "sky runtime pitch marker")
            raised = [block for block in re.findall(r"^thing \{(.*?)\}", text, re.M) if "type = 31000;" in block and "height = 160.0;" in block]
            require(len(raised) == 4, "four raised source-room sky sprites")
    require(content["models/fixture/cube.md2"][:8] == b"IDP2\x08\0\0\0", "MD2 signature/version")
    require(content["models/fixture/cube.md3"][:8] == b"IDP3\x0f\0\0\0", "MD3 signature/version")
    report = dict(fixture_sha256=sha(raw), bytes=len(raw), maps=6, voxel=check_voxel(content["voxels/fixture.kvx"]))
    if args.baseline_pk3:
        baseline = args.baseline_pk3.read_bytes()
        require(sha(baseline) == V1_SHA256, "baseline must be immutable qualified V1 archive")
        with zipfile.ZipFile(args.baseline_pk3) as archive:
            require(set(archive.namelist()) == set(content), "V2 preserves V1 member inventory")
            changed = {name for name in content if archive.read(name) != content[name]}
            require(changed == V2_CHANGED_MEMBERS, "only sky map, voxel and manifest may change from V1: " + repr(changed))
            previous = json.loads(archive.read("fixtures.json"))
            before = next(item for item in previous["maps"] if item["map"] == "GLTEST05")
            after = next(item for item in manifest["maps"] if item["map"] == "GLTEST05")
            require(before == after, "GLTEST05 camera/expectations/counts remain exact")
        report.update(baseline_sha256=sha(baseline), changed_members=sorted(changed), gltest05_byte_identical=True)
    if args.repeat_pk3:
        require(raw == args.repeat_pk3.read_bytes(), "second generation must be byte-identical")
        report["repeat_byte_identical"] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-pk3", type=Path, required=True)
    parser.add_argument("--baseline-pk3", type=Path)
    parser.add_argument("--repeat-pk3", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = check(args)
    text = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()

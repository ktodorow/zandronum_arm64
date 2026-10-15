#!/usr/bin/env python3
"""Check preserved histories and compare imported tree with official Hg archive."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tarfile

UPSTREAM = "28f736fb3f77bf0b767ec5468191bc05d0fccbb9"
LEGACY = "92dad02ccd1abe1ed49f93e6f7c16641cf0f20d1"
TREE = "951f47fc68595e7e136026c494e6822e22ac1de4"
ARCHIVE_SHA256 = "19d453b50a7c7abd2b2ce699aae52a696594b1b3ecffa3b024ce49107f6c4865"
ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("official_archive", type=Path)
    args = parser.parse_args()
    for ref, expected in (("legacy-arm-tip", LEGACY), ("upstream/ZA_3.2.1", UPSTREAM)):
        if git("rev-parse", ref).decode().strip() != expected:
            raise SystemExit(f"Unexpected {ref}")
        git("merge-base", "--is-ancestor", ref, "HEAD")
    for ref in (UPSTREAM, "baseline/3.2.1-import"):
        if git("rev-parse", ref + "^{tree}").decode().strip() != TREE:
            raise SystemExit(f"Unexpected baseline tree: {ref}")
    # This checksum records our download, not an upstream-published signature.
    if hashlib.sha256(args.official_archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise SystemExit("Official archive differs from recorded download")
    entries = git("ls-tree", "-rz", UPSTREAM).split(b"\0")
    files = {}
    for entry in filter(None, entries):
        meta, path = entry.split(b"\t", 1)
        mode, kind, oid = meta.split()
        files[path.decode()] = (mode.decode(), oid.decode())
    seen, failures = set(), []
    with tarfile.open(args.official_archive, "r|bz2") as archive:
        for member in archive:
            if not (member.isfile() or member.issym()):
                continue
            path = member.name.split("/", 1)[1]
            if path == ".hg_archival.txt":
                continue
            seen.add(path)
            data = member.linkname.encode() if member.issym() else archive.extractfile(member).read()
            mode, expected = files.get(path, ("", ""))
            actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            expected_mode = "120000" if member.issym() else "100755" if member.mode & 0o111 else "100644"
            if actual != expected or mode != expected_mode:
                failures.append(path)
    failures += sorted(files.keys() ^ seen)
    if failures:
        raise SystemExit("Baseline mismatches: " + ", ".join(failures))
    print(f"PASS: both histories preserved; {len(files)} official files and modes match")


if __name__ == "__main__":
    main()

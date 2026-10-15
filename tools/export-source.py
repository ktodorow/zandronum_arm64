#!/usr/bin/env python3
"""Export committed source with Git provenance, without requiring Hg at build time."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="destination .tar (committed files only)")
    parser.add_argument("--ref", default="HEAD", help="commit to export; default HEAD")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    commit = subprocess.check_output(
        ["git", "rev-parse", "--verify", args.ref + "^{commit}"], cwd=root, text=True
    ).strip()
    subprocess.run(
        ["git", "archive", "--format=tar", "--prefix=zandronum-arm64/",
         "--output=" + str(args.output.resolve()), commit], cwd=root, check=True
    )
    print(f"Exported committed source {commit} to {args.output.resolve()}")


if __name__ == "__main__":
    main()

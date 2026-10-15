#!/usr/bin/env python3
"""Exercise generated revision and compiled engine identity across source formats."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def run(args, cwd, **kwargs):
    return subprocess.check_output(args, cwd=cwd, text=True, **kwargs).strip()


class SourceIdentity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.source = self.base / "checkout"
        self.source.mkdir()
        for name in (".gitattributes", "source-provenance.txt", "src/gitinfo.cpp", "src/version.h",
                     "tools/updaterevision/updaterevision.c", "tools/updaterevision/release_identity.h",
                     "tools/export-source.py"):
            target = self.source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        (self.source / ".gitignore").write_text("src/gitinfo.h\n")
        self.git("init", "-q")
        self.git("config", "user.name", "Source identity test")
        self.git("config", "user.email", "source-identity@example.invalid")
        self.commit("fixture")
        self.tool = self.base / "updaterevision"
        run(shlex.split(os.environ.get("CC", "cc")) + ["-std=c99", "-Wall", "-Wextra", "-Werror",
            "tools/updaterevision/updaterevision.c", "-o", str(self.tool)], self.source)

    def git(self, *args):
        return run(["git", *args], self.source)

    def commit(self, message):
        self.git("add", ".")
        self.git("commit", "-qm", message)
        return self.git("rev-parse", "HEAD")

    def generate(self, source=None, env=None):
        source = source or self.source
        return run([str(self.tool), "src/gitinfo.h"], source, env=env)

    def identity(self, source=None):
        source = source or self.source
        probe = self.base / "probe.cpp"
        probe.write_text('#include <cstdio>\n#include "version.h"\nint main() {\n'
                         'printf("%d\\n%d\\n%s\\n%s\\n%s\\n%s\\n%s\\n%s\\n", '
                         'GetRevisionNumber(), NETGAMEVERSION, GetGitHash(), GetGitTime(), '
                         'GetVersionStringRev(), GetPortHash(), GetPortDescription(), GetPortTime());\n}\n')
        binary = self.base / "probe"
        run(shlex.split(os.environ.get("CXX", "c++")) + ["-Wall", "-Wextra", "-Werror", "-Isrc",
            "src/gitinfo.cpp", str(probe), "-o", str(binary)], source)
        result = run([str(binary)], source).splitlines()
        self.assertEqual(result[:5], ["1754343618", "194", "a701547f4162", "250804-2140", "3.2.1-r250804-2140"])
        return result[5:]

    def test_git_commits_dirty_state_and_stale_header(self):
        self.generate()
        first = self.identity()
        self.assertEqual(first[0], self.git("rev-parse", "HEAD"))
        self.assertNotIn("dirty", first[1])
        header = self.source / "src/gitinfo.h"
        before = header.stat().st_mtime_ns
        self.generate()
        self.assertEqual(header.stat().st_mtime_ns, before)
        header.write_text(header.read_text().replace("1754343618", "0"))
        self.generate()
        self.identity()
        (self.source / "port-change.txt").write_text("later port change\n")
        second = self.commit("later port commit")
        self.generate()
        self.assertEqual(self.identity()[0], second)
        self.assertNotEqual(first[0], second)
        (self.source / "port-change.txt").write_text("dirty tracked change\n")
        self.generate()
        self.assertIn("dirty", self.identity()[1])

    def test_archive_reproducible_and_builds_without_git_or_hg(self):
        paths = [self.base / name for name in ("one.tar", "two.tar")]
        for path in paths:
            run([os.sys.executable, "tools/export-source.py", str(path)], self.source)
        self.assertEqual(paths[0].read_bytes(), paths[1].read_bytes())
        exported = self.base / "exported"
        with tarfile.open(paths[0]) as archive:
            archive.extractall(exported)
        source = exported / "zandronum-arm64"
        env = dict(os.environ, PATH="")
        self.generate(source, env)
        result = self.identity(source)
        self.assertEqual(result[0], self.git("rev-parse", "HEAD"))
        self.assertNotIn("dirty", result[1])
        # An enclosing unrelated Git checkout cannot replace archive provenance.
        run(["git", "init", "-q", str(exported)], self.base)
        self.generate(source)
        self.assertEqual(self.identity(source), result)
        # Importing exported source into a new Git repo resumes live provenance.
        run(["git", "init", "-q", str(source)], self.base)
        self.source = source
        self.git("config", "user.name", "Source identity test")
        self.git("config", "user.email", "source-identity@example.invalid")
        imported = self.commit("import archived source")
        self.generate()
        self.assertEqual(self.identity()[0], imported)

    def test_unidentified_source_fails_even_with_stale_header(self):
        self.generate()
        shutil.rmtree(self.source / ".git")
        result = subprocess.run([str(self.tool), "src/gitinfo.h"], cwd=self.source,
                                env=dict(os.environ, PATH=""), capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot identify port source", result.stderr)


if __name__ == "__main__":
    unittest.main()

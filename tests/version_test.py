#!/usr/bin/env python3
"""Exercise release discovery and compile the real application version code."""

import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]


class VersionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="taiga-version-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.env = os.environ.copy()
        self.env.pop("TAIGA_VERSION", None)
        self.env.update({
            "GIT_AUTHOR_NAME": "Version test",
            "GIT_AUTHOR_EMAIL": "version-test@example.invalid",
            "GIT_COMMITTER_NAME": "Version test",
            "GIT_COMMITTER_EMAIL": "version-test@example.invalid",
        })
        (self.source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.21)
include("{REPO.as_posix()}/cmake/TaigaVersion.cmake")
project(VersionTest VERSION ${{TAIGA_VERSION_NUMERIC}} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
taiga_add_version_definitions()
add_executable(version_probe "{REPO.as_posix()}/src/taiga/version.cpp" probe.cpp)
target_include_directories(version_probe PRIVATE
    "{REPO.as_posix()}/src" "{REPO.as_posix()}/deps/semaver/include")
file(WRITE "${{CMAKE_BINARY_DIR}}/versions.txt"
    "${{PROJECT_VERSION}}\n${{TAIGA_VERSION_STRING}}\n${{TAIGA_VERSION_DEBIAN}}\n")
install(TARGETS version_probe RUNTIME DESTINATION bin)
set(CPACK_PACKAGE_VERSION "${{TAIGA_VERSION_STRING}}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${{TAIGA_VERSION_DEBIAN}}")
set(CPACK_PACKAGE_CONTACT "version-test@example.invalid")
include(CPack)
''')
        (self.source / "probe.cpp").write_text(
            '#include <iostream>\n#include "taiga/version.hpp"\n'
            'int main() { std::cout << taiga::version().to_string() << "\\n"; }\n'
        )

    def run_command(self, *args, cwd=None, success=True):
        result = subprocess.run(
            args, cwd=cwd or self.source, env=self.env,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result.stdout

    def init_git(self, tag=None):
        self.run_command("git", "init", "-q")
        self.run_command("git", "add", "CMakeLists.txt", "probe.cpp")
        self.run_command("git", "-c", "commit.gpgsign=false", "commit", "-qm", "fixture")
        if tag:
            self.run_command("git", "-c", "tag.gpgsign=false", "tag", tag)

    def check_version(self, numeric, full, debian, override=None):
        build = self.root / "build"
        args = ["cmake", "-S", str(self.source), "-B", str(build), "-G", "Ninja"]
        if override is not None:
            args.append(f"-DTAIGA_VERSION={override}")
        self.run_command(*args)
        self.assertEqual((build / "versions.txt").read_text().splitlines(),
                         [numeric, full, debian])
        self.run_command("cmake", "--build", str(build))
        self.assertEqual(self.run_command(str(build / "version_probe")).strip(), full)
        return build

    def test_stable_tag(self):
        self.init_git("v2.1.0")
        self.check_version("2.1.0", "2.1.0", "2.1.0")

    def test_prerelease_tag_and_packages(self):
        self.init_git("v2.1.0-rc.1")
        build = self.check_version("2.1.0", "2.1.0-rc.1", "2.1.0~rc.1")
        self.run_command("cpack", "-G", "TGZ", cwd=build)
        self.assertTrue(list(build.glob("*-2.1.0-rc.1-*.tar.gz")))
        self.run_command("cpack", "-G", "DEB", cwd=build)
        package, = build.glob("*.deb")
        unpacked = self.root / "deb-control"
        unpacked.mkdir()
        self.run_command("cmake", "-E", "tar", "xf", str(package), cwd=unpacked)
        with tarfile.open(unpacked / "control.tar.gz") as archive:
            control = next(member for member in archive if Path(member.name).name == "control")
            stream = archive.extractfile(control)
            assert stream is not None
            with stream:
                self.assertIn("Version: 2.1.0~rc.1", stream.read().decode().splitlines())
        if shutil.which("dpkg"):
            self.run_command("dpkg", "--compare-versions", "2.1.0~rc.1", "lt", "2.1.0")

    def test_two_component_tag(self):
        self.init_git("v2.1")
        self.check_version("2.1.0", "2.1.0", "2.1.0")

    def test_two_component_override(self):
        self.check_version("2.1.0", "2.1.0", "2.1.0", override="2.1")

    def test_environment_prerelease(self):
        self.env["TAIGA_VERSION"] = "2.1-rc.1"
        self.check_version("2.1.0", "2.1.0-rc.1", "2.1.0~rc.1")

    def test_override_takes_precedence(self):
        self.init_git("v2.1.0")
        self.env["TAIGA_VERSION"] = "2.2.0"
        self.check_version("2.3.0", "2.3.0-beta.2", "2.3.0~beta.2", override="2.3.0-beta.2")

    def test_source_archive_fallback(self):
        self.check_version("2.0.1", "2.0.1", "2.0.1")

    def test_git_without_tags_fallback(self):
        self.init_git()
        self.check_version("2.0.1", "2.0.1", "2.0.1")

    def test_branch_history_after_fetch(self):
        self.init_git("v2.2.0-rc.1")
        self.run_command("git", "-c", "commit.gpgsign=false", "commit",
                         "--allow-empty", "-qm", "after tag")
        clone = self.root / "clone"
        self.run_command("git", "clone", "--depth=1", self.source.as_uri(), str(clone))
        self.source = clone
        self.assertEqual(self.run_command("git", "tag").strip(), "")
        # Equivalent history availability to actions/checkout's fetch-depth: 0.
        self.run_command("git", "fetch", "--unshallow", "--tags")
        self.check_version("2.2.0", "2.2.0-rc.1", "2.2.0~rc.1")

    def test_invalid_override_fails_during_configuration(self):
        for version in ("garbage", "2.1.0-", "2.01.0", "2.1.0-extra..part"):
            with self.subTest(version=version):
                output = self.run_command(
                    "cmake", "-S", str(self.source), "-B", str(self.root / "invalid"),
                    f"-DTAIGA_VERSION={version}", success=False,
                )
                self.assertIn("Invalid Taiga version", output)


if __name__ == "__main__":
    unittest.main(verbosity=2)

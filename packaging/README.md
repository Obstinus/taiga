# Linux packages

The project uses CPack for the first Linux distribution artifacts. After a
normal CMake configure and build, create both supported package formats with:

```sh
cpack --config build/CPackConfig.cmake -G TGZ
cpack --config build/CPackConfig.cmake -G DEB
```

Packages are written to `build/packages/`. The Debian package obtains shared
library dependencies from the built executable; distro repositories remain
responsible for providing Qt 6, DBus, SQLite, and the desktop Secret Service.

## Build versions

CMake uses the nearest reachable `v*` version tag. Fetch the complete Git history
and tags when building a branch; a source archive or checkout without a reachable
version tag falls back to `2.0.1`.

Use `-DTAIGA_VERSION=2.1.0-rc.1` to override discovery, or set the `TAIGA_VERSION`
environment variable. The CMake option takes precedence. Versions with only major
and minor components are normalized: `2.1` becomes `2.1.0`.

The application and TGZ package retain prerelease suffixes such as `2.1.0-rc.1`.
Debian packages use `2.1.0~rc.1` so that the final `2.1.0` sorts as a newer version.

## Arch Linux

The stable Arch recipe is in [`arch/PKGBUILD`](arch/PKGBUILD). It checks out the
matching `v2.0.1` tag, initializes the project's submodules, and installs Taiga
under `/usr` with portable mode disabled:

```sh
cd packaging/arch
makepkg -si
```

The recipe uses `qt6-tools` to include translations. `gnome-keyring` and
`mpv-mpris` are optional runtime dependencies for credential storage and mpv
playback detection.

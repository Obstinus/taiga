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

## Arch Linux

The stable Arch recipe is in [`arch/PKGBUILD`](arch/PKGBUILD). It checks out the
matching `v2.0.0` tag, initializes the project's submodules, and installs Taiga
under `/usr` with portable mode disabled:

```sh
cd packaging/arch
makepkg -si
```

The recipe uses `qt6-tools` to include translations. `gnome-keyring` and
`mpv-mpris` are optional runtime dependencies for credential storage and mpv
playback detection.

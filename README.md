# Taiga for Linux

[![License: GPL-3.0](https://img.shields.io/github/license/erengy/taiga)](LICENSE)

This repository contains an experimental Linux port of [Taiga](https://taiga.moe),
an anime library and progress tracker originally developed for Windows. The port
uses Qt and detects media playback through MPRIS. It includes integrations
with [AniList](https://anilist.co), [Kitsu](https://kitsu.app), and
[MyAnimeList](https://myanimelist.net); desktop and service workflows still need
broader testing on Linux.

Upstream Windows releases do not represent the feature set or release status of
this Linux branch.

## Current functionality

- Detect local episodes and supported streaming pages from MPRIS-enabled players and match them
  against the local catalog.
- Display the current episode and update progress at 95% of the reported media duration,
  when identification succeeds and synchronization is enabled.
- Browse and manage the anime list and local library.
- Configure appearance, title language, library folders, detection interval, player
  exclusions, HTTP proxy, and cache maintenance.
- Browse and search torrent RSS feeds, filter releases, show SeaDex release coloring,
  and open magnets or downloaded torrent files in the desktop's BitTorrent client.
- Store AniList access tokens in the Linux desktop keyring.
- Optionally announce completed episodes through Discord Rich Presence, an HTTP POST endpoint, or
  a standard IRC connection. These integrations are disabled individually by default.

The Windows mIRC DDE protocol is not available on Linux; the Linux port uses a standard IRC
connection instead. Torrent transfers are handled by an external client, and releases are not
opened automatically.

## Build and run

Build dependencies:

- CMake 3.21 or newer and Ninja.
- A compiler and standard library with C++23 support, including `std::ranges::to`.
- Qt 6 development packages: Core, Gui, Widgets, Network, Sql, Svg, Concurrent,
  and DBus. LinguistTools is needed if translations are enabled.
- Git, including the repository's submodules.

The current development build has been compiled with GCC 16.2.1 and Qt 6.11.2.
CMake declares Qt 6.8 as its baseline, but compatibility with that older version
has not been verified.

```sh
git clone --branch linux-port --recurse-submodules https://github.com/Obstinus/taiga.git
cd taiga
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTAIGA_ENABLE_TRANSLATIONS=OFF
cmake --build build --parallel 4
./bin/taiga
```

For an existing checkout, run `git submodule update --init --recursive` before
configuring. Portable mode is enabled by default and stores application data in
`bin/data/`. Configure with `-DTAIGA_PORTABLE=OFF` to use the user's application
data location instead. Enable translations with `-DTAIGA_ENABLE_TRANSLATIONS=ON`
when LinguistTools is installed.

Common C++ and Qt headers are precompiled by default to speed up application
and GUI compilation. Disable this with `-DTAIGA_ENABLE_PCH=OFF` when checking
header dependencies or using tools that do not support precompiled headers.
Keep the build directory between builds so Ninja only rebuilds changed files.

Run Taiga and the player in the same desktop session. The player must expose an
MPRIS interface; mpv needs an MPRIS integration such as mpv-mpris. Detection also
needs matching catalog entries in Taiga's database. Automatic progress updates
require the player to report playback position and duration.

## AniList authentication and credential storage

Install `secret-tool` (from libsecret) and run an unlocked Secret Service provider,
such as GNOME Keyring, in your desktop session.

In **Settings → Accounts**, select AniList and choose **Authenticate**. Complete
authorization on AniList in your browser and paste the access token into Taiga.
Use **Replace AniList token...** to replace an expired or revoked token.

The token is stored in the desktop keyring. On startup, a token from an older
`accounts.json` is migrated and read back from the keyring before its plaintext
entry is removed. The data directory is restricted to `0700` and the account
file to `0600`.

If secure storage fails, Taiga reports the error. A newly entered token remains
available only for the current session and is not saved to JSON. An existing
legacy token is retained until migration succeeds. Moving the data directory
requires authorizing again because keyring entries are scoped to its path.

This keyring integration currently covers AniList only. Other services still
use the protected account file. Do not commit or share runtime account files,
and remember that migration does not remove credentials from old backups.
See [credential storage and security tests](tests/ACCOUNTS_SECURITY.md) for details.

## Tests

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTAIGA_ENABLE_TRANSLATIONS=OFF \
  -DTAIGA_BUILD_MPRIS_PROBE=ON \
  -DTAIGA_BUILD_TORRENT_TESTS=ON
cmake --build build --parallel 4 --target all anitomy-tests
dbus-run-session -- ./bin/taiga-mpris-probe --self-test
ctest --test-dir build --output-on-failure
bash tests/run_accounts_security_test.sh --keyring
```

Torrent tests use temporary files and loopback HTTP servers. Credential tests
use synthetic tokens, a separate D-Bus session, and temporary keyring storage;
the keyring test also requires `pkg-config` and `gnome-keyring-daemon`.
Use a separate D-Bus session for isolated tests, but run the application normally
to access your real player and desktop keyring.

Additional workflow details: [torrent usage](tests/TORRENTS.md) and
[settings acceptance checks](tests/SETTINGS.md).

## Packaging

Linux installs include a desktop entry, the Taiga icon, and AppStream metadata. CPack produces a
tarball and a Debian package after configuring and building:

```sh
cpack --config build/CPackConfig.cmake -G TGZ
cpack --config build/CPackConfig.cmake -G DEB
```

Artifacts are written to `build/packages/`. The Linux workflow in
`.github/workflows/linux.yml` builds, tests, and publishes both artifacts for each branch update.
An Arch Linux recipe is available at [`packaging/arch/PKGBUILD`](packaging/arch/PKGBUILD).

## Links

- [Upstream changelog](https://github.com/erengy/taiga/wiki/Changelog)
- [Upstream contribution guidelines](https://github.com/erengy/taiga/wiki/Guidelines)
- [Upstream compilation guide](https://github.com/erengy/taiga/wiki/How-to-Compile)

### Related projects

- [Anime relations](https://github.com/erengy/anime-relations) (episode redirections)
- [Anisthesia](https://github.com/erengy/anisthesia) (media detection library)
- [Anitomy](https://github.com/erengy/anitomy) (anime video filename parser)
- [taiga.moe](https://github.com/erengy/taiga-moe) (home page of Taiga)

## License

Taiga is licensed under [GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.html).

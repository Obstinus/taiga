# Linux validation

Configure and build from the repository root:

```sh
cmake -S . -B /tmp/taiga-linux-build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTAIGA_ENABLE_TRANSLATIONS=OFF -DTAIGA_BUILD_MPRIS_PROBE=ON
cmake --build /tmp/taiga-linux-build --parallel 4
dbus-run-session -- ./bin/taiga-mpris-probe --self-test
```

For a system installation, disable portable mode so application data is stored
under the user's XDG data directory, then install the binary, desktop entry and
hicolor icon:

```sh
cmake -S . -B /tmp/taiga-linux-install-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DTAIGA_PORTABLE=OFF \
  -DTAIGA_ENABLE_TRANSLATIONS=OFF
cmake --build /tmp/taiga-linux-install-build --parallel 4
sudo cmake --install /tmp/taiga-linux-install-build
```

The install prefix defaults to `/usr/local`; pass `-DCMAKE_INSTALL_PREFIX=...`
at configure time to select another prefix. Enable translations when the Qt
LinguistTools component is installed.

Run `./bin/taiga --debug` and an MPRIS-enabled player in the same desktop
session. `./bin/taiga-mpris-probe --real` prints active MPRIS services and their media.
Do not wrap the application in a separate `dbus-run-session` when testing a
desktop player. With mpv, install mpv-mpris and allow normal script loading;
`--no-config` disables the system configuration used to load it.

The debug log records detected title, episode number and the matched anime ID.
Identification needs catalog entries in the local database. An empty database
can parse filenames but cannot resolve an anime ID.

MPRIS services in `Playing` and `Paused` state are kept available to the
detector. When exposed by the player, `mpris:length` and `Position` are
converted from microseconds to milliseconds; pausing therefore does not erase
the current Now Playing item.

## Remaining acceptance checks

See [TORRENTS.md](TORRENTS.md) for the Qt torrent workflow and isolated tests.

- Verify catalog identification and the Now Playing panel with a populated database.
- Verify pause/resume, episode changes, player exit and multiple players.
- Verify tray behavior and settings persistence in a graphical session.
- Verify service authentication, list import and queued updates.
- Verify translations with LinguistTools enabled.

The Qt Now Playing panel currently displays a fixed `List update in 00:00`
message. Its detection signal is connected to the panel, but no automatic
watch-progress controller is connected. Automatic progress updates must not
be treated as a validated feature of this port.

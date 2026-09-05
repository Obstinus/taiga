# Linux validation

Configure and build from the repository root:

```sh
cmake -S . -B /tmp/taiga-linux-build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTAIGA_ENABLE_TRANSLATIONS=OFF -DTAIGA_BUILD_MPRIS_PROBE=ON
cmake --build /tmp/taiga-linux-build --parallel 4
dbus-run-session -- ./bin/taiga-mpris-probe --self-test
```

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
- Add Linux installation rules, desktop entry and icon installation.
- Verify translations with LinguistTools enabled.

The Qt Now Playing panel currently displays a fixed `List update in 00:00`
message. Its detection signal is connected to the panel, but no automatic
watch-progress controller is connected. Automatic progress updates must not
be treated as a validated feature of this port.

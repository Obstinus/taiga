# Torrents

The Qt Torrents page reads RSS feeds and opens selected releases in the desktop's
default BitTorrent client. It does not implement a BitTorrent transfer engine.

## Use

Open **Torrents**, choose the active feed from the toolbar and refresh it or enter a
search. Add or remove RSS feeds in **Settings**; the Nyaa feed remains the default,
and the search URL uses `%title%` for the encoded query.

Filter releases by title, release group or resolution. Check releases and open
them to download their `.torrent` files and hand them to the default client.
Magnet links go directly to that client. Configure the desktop's `.torrent` and
`magnet:` associations if no client opens.
The configured download directory stores `.torrent` metainfo files; choose the
destination of the actual media in your BitTorrent client.

Discarded and successfully opened releases are remembered across restarts. Torrent
settings and archived IDs are kept in `data/torrents.json` under Taiga's data
directory. Torrent files default to `data/torrents/`. This state is separate from
the anime database and service accounts. Malformed existing state is not overwritten.

Automatic refresh is optional and runs after the Torrents page has been initialized.
When the RSS item includes an info hash, Taiga checks the public SeaDex API at
`releases.moe` and colors matching rows using the same convention as NyaaBlue:
best releases are blue and alternative releases are orange. A SeaDex timeout or
API error leaves the feed usable without coloring.
The SeaDex RSS feed itself exposes opaque release IDs; Taiga replaces them with
file and release-group metadata from the API and omits entries whose hash is redacted.

It does not automatically open or download releases. Downloads are asynchronous;
HTTP failures, timeouts, oversized responses and invalid metainfo are reported.
Downloaded file names are sanitized and unique, so existing files are not overwritten.

The old Windows v1 filter-rule engine and client-specific remote-control integrations
are not imported by this implementation.

## Isolated tests

```sh
cmake -S . -B /tmp/taiga-linux-build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTAIGA_ENABLE_TRANSLATIONS=OFF -DTAIGA_BUILD_TORRENT_TESTS=ON
cmake --build /tmp/taiga-linux-build --parallel 4
ctest --test-dir /tmp/taiga-linux-build --output-on-failure -R torrent
```

Tests use synthetic RSS/metainfo, temporary directories and loopback HTTP servers.
They do not download media, contact a public tracker, open a real torrent client or
modify the user's application data. Real desktop association behavior must also be
checked in a graphical session with a user-selected torrent.

The widget test runs with Qt's offscreen platform and intercepts magnet opening.
It checks filtering, sorted selection, discard/restore and archive persistence.
The feed tests also check cancellation, replacement of in-flight requests,
response-size limits, URL encoding and duplicate identities.

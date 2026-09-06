# Qt settings

The Qt settings dialog exposes the implemented application settings:

- Application: system/light/dark color scheme.
- Anime List: title language and synchronization.
- Library: add/remove episode search folders.
- Recognition: polling interval (250–60000 ms).
- Media players: enable/disable known players and browsers, and maintain extra
  MPRIS identity/service exclusions without losing unknown saved entries.
- Advanced: HTTP proxy address and credentials, with address validation.
- Cache: rebuild the recognition index, release images from memory, and open
  the on-disk image cache folder.

Accounts retains its authentication/synchronization actions. Torrents,
Downloads, and Filters open the existing torrent configuration dialog, which
saves independently. Opening that dialog does not apply pending edits in the
main settings dialog.

Streaming and Sharing/Discord/HTTP/mIRC were navigation placeholders rather
than working Qt integrations. They are omitted until their backends are ported.
Linux recognition still only accepts local files through MPRIS, including when
those files are opened in a browser.

## Acceptance checks

Use an isolated data directory/account when checking settings.

1. Select every navigation entry. Each must show its corresponding controls;
   there must be no generic unavailable page or leftover Accounts TODO label.
2. Change the polling interval, title language, synchronization, folders,
   player selections, and proxy credentials. Cancel and reopen: values must
   remain unchanged. Repeat with OK: values must persist on reopening.
3. Enter an unsupported proxy scheme, missing host, invalid port, or URL with
   a path/query. OK must report the error and leave the dialog open without
   saving any pending edits. Empty proxy and `localhost:8080` must be accepted.
4. Save a custom disabled MPRIS identity, reopen, and save again. It must remain
   excluded. Remove it explicitly and confirm that it is no longer excluded.
5. Save a new polling interval and verify the active detector uses it. Theme
   changes should take effect immediately. Restart for proxy changes and to
   refresh existing library/list views after changing folders/title language.
6. Run both cache actions; they must report completion without deleting the
   anime database, account credentials, or images stored on disk.
7. Edit a setting, open torrent configuration, close it, then cancel the main
   settings dialog. The pending main-dialog edit must not have been saved.

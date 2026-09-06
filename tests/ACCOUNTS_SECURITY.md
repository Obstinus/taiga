# AniList credential storage on Linux

AniList tokens are stored in the session's Secret Service keyring through
`secret-tool` (provided by libsecret). GNOME Keyring or another Secret Service
provider must be available and unlocked. Tokens travel to the helper through
stdin, never command-line arguments or environment variables. Keyring access
is asynchronous and each helper is terminated after 30 seconds.

The keyring entry is scoped to the absolute application data directory, using
its SHA-256 hash. Moving that directory requires authorizing again. Accounts
metadata remains in accounts.json; the data directory is restricted to 0700 and
the account file to 0600. This change protects AniList token persistence; other
services still use the account file.

On startup, an existing AniList token is written to the keyring and read back.
Only a successful matching read permits removing the old JSON entry. If this
fails, the old file remains protected and an error is shown. New tokens remain
in memory for the current process when secure storage fails; they are never
written to JSON as a fallback. Authentication is revalidated each process;
a saved `authenticated` flag is not trusted. Settings provides a replacement
action for expired or revoked AniList tokens.

Deleting the old entry does not erase historical backups or filesystem
snapshots. Keyring storage also does not isolate a token from a compromised
process running as the same desktop user.

Run isolated tests (Qt development tools, libsecret's secret-tool, dbus-run-session,
and gnome-keyring-daemon required for the keyring test):

```sh
bash tests/run_accounts_security_test.sh --keyring
```

The script uses a temporary data directory. Its failure test uses an unavailable
D-Bus address; its success test uses a separate bus and temporary XDG keyring
storage. It checks migration, persistence, replacement, deletion, restricted
file permissions, runtime authentication state, and no plaintext fallback.

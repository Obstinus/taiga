#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_build_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_build_dir"' EXIT
qt_libexec="$(pkg-config --variable=libexecdir Qt6Core)"
"$qt_libexec/moc" "$repo_dir/src/taiga/accounts.hpp" -o "$test_build_dir/moc_accounts.cpp"
c++ -std=c++23 -fPIC -I"$repo_dir/src" \
  "$repo_dir/tests/accounts_security_test.cpp" \
  "$repo_dir/src/taiga/accounts.cpp" "$repo_dir/src/base/settings.cpp" \
  "$repo_dir/src/sync/anilist/anilist_ratings.cpp" \
  "$repo_dir/src/sync/kitsu/kitsu_ratings.cpp" \
  "$test_build_dir/moc_accounts.cpp" \
  $(pkg-config --cflags --libs Qt6Core Qt6Network) -o "$test_build_dir/test"
"$test_build_dir/test"
if [[ "${1:-}" == "--keyring" ]]; then
  # The isolated bus and XDG directories ensure no access to the user's real keyring.
  mkdir -m 700 "$test_build_dir/keyring-home" "$test_build_dir/runtime"
  env XDG_DATA_HOME="$test_build_dir/keyring-home/data" \
    XDG_CONFIG_HOME="$test_build_dir/keyring-home/config" XDG_RUNTIME_DIR="$test_build_dir/runtime" \
    dbus-run-session -- bash -c '
      printf "%s" "test-keyring-password" | gnome-keyring-daemon --unlock --components=secrets >/dev/null
      "$1" --keyring
    ' bash "$test_build_dir/test"
fi

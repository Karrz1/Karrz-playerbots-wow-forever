#!/usr/bin/env bash
# Lists what this tree changes relative to the pinned advocaite commit (UPSTREAM_PIN), for doc/UPSTREAM.md and for judging a merge.
#   scripts/upstream-diff.sh hooks     bot module call sites inside upstream-owned files (the file to keep small)
#   scripts/upstream-diff.sh modified  upstream files this tree edits (status M), per directory
#   scripts/upstream-diff.sh added     files this tree adds outside the bot module (status A)
# Needs the pin commit in the local object store: git fetch https://github.com/advocaite/TrinityCore forever
set -euo pipefail
cd "$(dirname "$0")/.."
pin=$(sed -n 's/^commit:[[:space:]]*//p' UPSTREAM_PIN | head -n1)
own='^src/modules/PlayerbotV2/|^src/server/game/Playerbot/'
case "${1:-}" in
  hooks)
    grep -rlE 'Playerbot::|PlayerbotAPI|PlayerbotHooks|Playerbot/' src --include='*.cpp' --include='*.h' | grep -Ev "$own" | sort |
      while read -r f; do printf '%4d  %s\n' "$(grep -cE 'Playerbot::|PlayerbotAPI|PlayerbotHooks|Playerbot/' "$f")" "$f"; done ;;
  modified) git diff --name-status "$pin" HEAD -- src sql cmake CMakeLists.txt | awk '$1=="M"{print $2}' | grep -Ev "$own" | sort ;;
  added)    git diff --name-status "$pin" HEAD -- src sql cmake CMakeLists.txt | awk '$1=="A"{print $2}' | grep -Ev "$own" | sort ;;
  *) echo "usage: $0 hooks|modified|added" >&2; exit 2 ;;
esac

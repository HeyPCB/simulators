#!/bin/sh
# SPDX-License-Identifier: MIT
# Security checks for one image, every platform it carries, before it is
# published (README.md, "Security"):
#   1. its build history (docker history --no-trunc), env and labels hold no secret
#      or internal name;
#   2. it runs as a non-root user;
#   3. its filesystem holds no secret-bearing file (.env, keys, .ssh, git or
#      docker credentials, .netrc, .npmrc, .pypirc), no compiler or build
#      tool, no package cache;
#   4. gitleaks and trufflehog find nothing in the files this repository's
#      layers add (/opt/heypcb-*), and neither do the deny patterns.
#
#   tools/scan.sh ghcr.io/heypcb/esp32-qemu:1.0.0 linux/amd64,linux/arm64
#
# Needs gitleaks and trufflehog on PATH (or GITLEAKS / TRUFFLEHOG set).
# SCAN_DENY_FILE may name extra patterns (one extended regex per line) that
# are not written in this repository: internal host or project names.
set -eu
IMG="$1"; PLATFORMS="$2"
GITLEAKS="${GITLEAKS:-gitleaks}"; TRUFFLEHOG="${TRUFFLEHOG:-trufflehog}"
DENY='BEGIN [A-Z ]*PRIVATE KEY|ghp_[A-Za-z0-9]{20}|github_pat_|gho_[A-Za-z0-9]{20}|xox[abp]-|AKIA[0-9A-Z]{16}|sk-[A-Za-z0-9]{20}|eyJhbGciOi|\.ts\.net|tailscale|supabase\.co|onrender\.com|srv-[a-z0-9]{20}|/home/[a-z]|/Users/[A-Za-z]'
if [ -n "${SCAN_DENY_FILE:-}" ] && [ -s "$SCAN_DENY_FILE" ]; then
  DENY="$DENY|$(grep -v '^#' "$SCAN_DENY_FILE" | grep -v '^$' | paste -sd'|')"
fi
fail=0
say() { printf '%s\n' "$*"; }
bad() { printf 'FAIL %s\n' "$*"; fail=1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for p in $(echo "$PLATFORMS" | tr ',' ' '); do
  say "== $IMG ($p)"
  docker image inspect --platform "$p" "$IMG" --format '{{json .Config}}' > "$work/config.json"
  docker history --platform "$p" --no-trunc --format '{{.CreatedBy}}' "$IMG" > "$work/history.txt"
  user=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("User",""))' "$work/config.json")
  case "$user" in ""|0|root|0:0|root:root) bad "runs as root (User=\"$user\")";; *) say "ok   user $user";; esac
  if grep -aiEq "$DENY" "$work/config.json" "$work/history.txt"; then
    bad "history/env/labels match a deny pattern:"; grep -aiEo "$DENY" "$work/config.json" "$work/history.txt" | sort -u | head
  else say "ok   history, env, labels: no deny pattern"; fi
  cid=$(docker create --platform "$p" "$IMG")
  rm -rf "$work/root"; mkdir -p "$work/root"
  docker export "$cid" | tar -x -C "$work/root" 2>/dev/null || true
  docker rm "$cid" >/dev/null
  found=$(cd "$work/root" && find . \( -name '.env' -o -name '*.env' -o -name 'id_rsa*' -o -name 'id_ed25519*' -o -name '*.pem' -o -name '*.key' \
      -o -name '.ssh' -o -name '.git-credentials' -o -name '.netrc' -o -name '.npmrc' -o -name '.pypirc' -o -name '.secrets*' \
      -o -path './root/.docker' -o -path '*/.docker/config.json' -o -name '.git' \) -not -path './etc/ssl/*' -not -path './usr/share/*' -not -path './proc/*' 2>/dev/null | head -20)
  [ -n "$found" ] && bad "secret-bearing files: $found" || say "ok   no secret-bearing files"
  tools=""
  for t in gcc cc g++ c++ make ninja meson cmake pkg-config git curl wget python3 perl pip; do
    [ -e "$work/root/usr/bin/$t" ] && tools="$tools $t"
  done
  # perl-base is part of Ubuntu's essential set; it is the only one allowed.
  tools=$(echo "$tools" | sed 's/ perl//')
  [ -n "$tools" ] && bad "build tools left in the image:$tools" || say "ok   no compiler, build tool, git, curl, wget, python or pip"
  lists=$(find "$work/root/var/lib/apt/lists" -type f 2>/dev/null | grep -v lock | wc -l)
  debs=$(find "$work/root/var/cache/apt" -name '*.deb' -o -name '*.bin' 2>/dev/null | wc -l)
  [ "$lists" -eq 0 ] && [ "$debs" -eq 0 ] && say "ok   no apt lists or package cache" || bad "apt lists $lists, cached packages $debs"
  for d in "$work"/root/opt/heypcb-*; do
    [ -d "$d" ] || continue
    if "$GITLEAKS" dir "$d" --no-banner --redact -l error >/dev/null 2>"$work/gl.txt"; then say "ok   gitleaks dir $(basename "$d"): no leaks"
    else bad "gitleaks dir $(basename "$d"):"; cat "$work/gl.txt"; fi
    "$TRUFFLEHOG" filesystem "$d" --no-update --json --no-verification 2>/dev/null > "$work/th.json" || true
    n=$(grep -c '"DetectorName"' "$work/th.json" || true)
    [ "$n" = 0 ] && say "ok   trufflehog filesystem $(basename "$d"): 0 findings" || { bad "trufflehog: $n findings"; head -c 2000 "$work/th.json"; }
    hits=$(find "$d" -type f -exec strings -n 5 {} + 2>/dev/null | grep -aiEo "$DENY" | sort -u | head)
    [ -z "$hits" ] && say "ok   deny patterns: none in $(basename "$d")" || bad "deny patterns in $(basename "$d"): $hits"
  done
done
exit "$fail"

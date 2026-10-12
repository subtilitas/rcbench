#!/usr/bin/env bash
# Runs the data check with --strict in a GitHub Actions job and shows each
# warning as an annotation of the run and as a line of its summary. A
# release tag newer than the data is such a warning.
#
#   bash tools/check-annotate.sh             exit code of the check: 1 on an error
#   bash tools/check-annotate.sh --warn-only exit 0; an error is a warning as well
#
# Run from configurator/core. Outside GitHub Actions the summary goes to
# /dev/null and the annotations are plain lines of the output.

set -u

warn_only=0
case "${1:-}" in
  '') ;;
  --warn-only) warn_only=1 ;;
  *) echo "usage: bash tools/check-annotate.sh [--warn-only]" >&2; exit 2 ;;
esac

summary="${GITHUB_STEP_SUMMARY:-/dev/null}"
out="$(mktemp)"
trap 'rm -f "$out"' EXIT

# The status of the pipe is the one of tee; the check is the first command.
node tools/check.ts --strict | tee "$out"
status="${PIPESTATUS[0]}"

warnings="$(grep '^warn ' "$out" || true)"
errors="$(grep '^error ' "$out" || true)"
if [ "$warn_only" = 1 ] && [ -n "$errors" ]; then
  warnings="$(printf '%s\n%s\n' "$warnings" "$errors" | grep -v '^$' || true)"
fi
if [ "$warn_only" = 1 ] && [ "$status" != 0 ] && [ -z "$errors" ]; then
  warnings="$(printf '%s\nwarn check: the data check ended with exit code %s\n' "$warnings" "$status" | grep -v '^$')"
fi

if [ -n "$warnings" ]; then
  printf '%s\n' "$warnings" | sed -E 's/^(warn|error) /::warning title=Configurator data::/'
  {
    echo '### Configurator data: warnings'
    echo
    printf '%s\n' "$warnings" | sed -E 's/^(warn|error) /- /'
  } >> "$summary"
fi

if [ "$warn_only" = 1 ]; then
  exit 0
fi
exit "$status"

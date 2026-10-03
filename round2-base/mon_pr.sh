#!/bin/bash
# Poll a PR: CI checks and Codex review activity after a given head commit.
# Usage: mon179.sh PR HEAD_SHA
export GH_TOKEN=$(tr -d '\r\n' < ~/github/.git_token_rcbench_openyge)
R=repos/subtilitas/rcbench; N=${1:-179}; HEAD=$2
BOT='chatgpt-codex-connector[bot]'
for i in $(seq 1 60); do
  checks=$(gh pr checks $N 2>/dev/null | awk -F'\t' '{print $2}' | sort | uniq -c | tr '\n' ' ')
  rid=$(gh api $R/pulls/$N/reviews --jq "[.[]|select(.user.login==\"$BOT\" and .commit_id==\"$HEAD\")|.id]|last" 2>/dev/null)
  if [ -n "$rid" ] && [ "$rid" != null ]; then inline=$(gh api $R/pulls/$N/reviews/$rid/comments --jq length 2>/dev/null); else inline=0; fi
  rev=$(gh api $R/pulls/$N/reviews --jq "[.[]|select(.user.login==\"$BOT\" and .commit_id==\"$HEAD\")|.state]|join(\",\")" 2>/dev/null)
  done_on=$(gh api $R/issues/$N/comments --jq "[.[]|select(.user.login==\"$BOT\" and (.body|contains(\"Review Summary\")))|.body]|last" 2>/dev/null | grep -o "Completed[^|]*|[^|]*\`${HEAD:0:7}\`" | head -1)
  running=$(gh api $R/issues/$N/comments --jq "[.[]|select(.user.login==\"$BOT\" and (.body|contains(\"Review Summary\")))|.body]|last" 2>/dev/null | grep -c "Running")
  reac=$(gh api $R/issues/$N/reactions --jq "[.[]|select(.user.login==\"$BOT\")|.content]|join(\",\")" 2>/dev/null)
  echo "$(date +%T) checks: $checks| inline on head: $inline | reviews on head: $rev | reactions: $reac | review of head done: ${done_on:+yes} | running: $running"
  if ! echo "$checks" | grep -q pending && [ -n "$checks" ] && { [ "${inline:-0}" != 0 ] || { [ -n "$done_on" ] && [ "${running:-0}" = 0 ]; }; }; then echo DONE; exit 0; fi
  sleep 60
done
echo TIMEOUT

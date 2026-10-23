#!/bin/bash
# Host test of firmware/components/net/dns_answer.c, the setup AP's DNS. The oracle is
# dig: it sends real queries (with EDNS, as phones do) to the harness on 127.0.0.1 and
# parses the replies. Malformed packets go through once, under the address sanitizer.
# Usage: firmware/test/dns/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$(cd "$HERE/../.." && pwd)
WORK=$(mktemp -d)
trap 'kill $SERVER 2>/dev/null || true; rm -rf "$WORK"' EXIT

cc -O1 -g -Wall -Werror -fsanitize=address,undefined -I"$FW/components/net" \
  "$HERE/harness.c" "$FW/components/net/dns_answer.c" -o "$WORK/harness"

PORT=$((20000 + RANDOM % 20000))
failed=0
check() {
  if [[ "$2" == "$3" ]]; then
    echo "ok  $1: $2"
  else
    echo "BAD $1: got '$2', want '$3'"
    failed=$((failed + 1))
  fi
}

"$WORK/harness" serve "$PORT" 5 >"$WORK/out" &
SERVER=$!
until grep -q ready "$WORK/out" 2>/dev/null; do sleep 0.1; done
ask() {
  dig @127.0.0.1 -p "$PORT" +tries=1 +time=2 "$@"
}
check "A captive.apple.com" "$(ask +short captive.apple.com A)" "10.47.0.1"
check "A connectivitycheck.gstatic.com" "$(ask +short connectivitycheck.gstatic.com)" "10.47.0.1"
check "A without EDNS" "$(ask +short +noedns www.msftconnecttest.com)" "10.47.0.1"
aaaa=$(ask captive.apple.com AAAA)
check "AAAA status" "$(grep -o 'status: [A-Z]*' <<<"$aaaa")" "status: NOERROR"
check "AAAA answers" "$(grep -o 'ANSWER: [0-9]*' <<<"$aaaa")" "ANSWER: 0"
a=$(ask captive.apple.com A)
check "A flags" "$(grep -o 'flags: [a-z ]*;' <<<"$a")" "flags: qr aa rd ra;"
wait $SERVER

once() {
  "$WORK/harness" once "$1"
}
check "short header" "$(once 1234)" "none"
check "a response" "$(once 12348100000100000000000003616263000001000100)" "none"
check "two questions" "$(once 12340100000200000000000003616263000001000100)" "none"
check "label past the end" "$(once 1234010000010000000000003f6162)" "none"
check "no type and class" "$(once 123401000001000000000000036162630000)" "none"
check "name pointer" "$(once 123401000001000000000000c00c00010001)" "none"
check "no zero label" "$(once 123401000001000000000000036162636465)" "none"

if [[ $failed != 0 ]]; then
  echo "$failed FAILED"
  exit 1
fi
echo "ALL PASS"

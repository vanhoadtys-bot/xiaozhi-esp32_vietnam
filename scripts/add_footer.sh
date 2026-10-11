#!/bin/bash
# Puts the credit line at the TOP of the Wi-Fi setup page. Safe to run repeatedly.
DIR=managed_components/78__esp-wifi-connect
TOP='<div class="vh-credit" style="text-align:center;padding:10px 12px 0;font-size:20px;opacity:.85">Designed by Van Hoa | Phone: 0981861795</div>'
FILES=$(find "$DIR" -name "*.html")
if [ -z "$FILES" ]; then
  echo "No .html files found in $DIR"
  exit 1
fi
for f in $FILES; do
  # remove any older credit line first
  sed -i 's#<div class="vh-credit"[^>]*>[^<]*</div>##g' "$f"
  if grep -q "<body" "$f"; then
    sed -i "s#<body\([^>]*\)>#<body\1>${TOP}#" "$f"
    echo "patched: $f"
  else
    echo "skipped (no <body>): $f"
  fi
done
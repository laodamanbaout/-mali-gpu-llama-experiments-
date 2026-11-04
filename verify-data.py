#!/usr/bin/env python3
"""Verify the curated research-file manifest without reading private files."""
from pathlib import Path
import hashlib
import json
import sys

root = Path(__file__).resolve().parent
records = json.loads((root / 'DATA-MANIFEST.json').read_text(encoding='utf-8'))
errors = []
for item in records:
    path = root / item['path']
    if not path.is_file():
        errors.append(f"missing: {item['path']}")
        continue
    data = path.read_bytes()
    if len(data) != item['size'] or hashlib.sha256(data).hexdigest() != item['sha256']:
        errors.append(f"mismatch: {item['path']}")
if errors:
    print('\n'.join(errors))
    sys.exit(1)
print(f'OK: {len(records)} curated research files match their SHA-256 manifest')

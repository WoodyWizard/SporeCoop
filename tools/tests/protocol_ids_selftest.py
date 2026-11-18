"""Every wire message must have a distinct byte ID (BUG-9085)."""
from pathlib import Path
import re

source = (Path(__file__).resolve().parents[2] / 'mods/coop/net/protocol.h').read_text()
entries = re.findall(r'^\s*(MSG_\w+)\s*=\s*(\d+)\s*,', source, re.M)
assert len(entries) >= 67, 'Message registry was not parsed'
used = {}
for name, number in entries:
    number = int(number)
    assert 0 < number < 256, (name, number)
    assert number not in used, f'{name} and {used.get(number)} share wire ID {number}'
    used[number] = name
print(f'PASS {len(entries)} unique wire message IDs')

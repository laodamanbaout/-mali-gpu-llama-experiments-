"""Summarize locally collected research logs; does not connect to the phone."""
from pathlib import Path
import re
import struct

HERE = Path(__file__).resolve().parent

def describe(label):
    log_path = HERE / (label + '.log')
    if not log_path.exists():
        return
    text = log_path.read_text(encoding='utf-8', errors='replace')
    splits = re.findall(r'## SPLIT #([0-9]+):\s+(\S+)', text)
    memory_path = HERE / (label + '-memory.tsv')
    available = []
    if memory_path.exists():
        for line in memory_path.read_text().splitlines():
            cols = line.split('\t')
            if len(cols) > 1 and cols[1].isdigit():
                available.append(int(cols[1]))
    print(label)
    if splits:
        print('  scheduler entries:', len(splits), 'backends:', sorted(set(b for _, b in splits)),
              'largest split number:', max(int(i) for i, _ in splits))
    if available:
        print('  MemAvailable samples:', len(available), 'minimum KiB:', min(available))
    match = re.search(r'generated_text_begin\s*(.*?)generated_text_end', text, re.S)
    if match:
        print('  generated text:', match.group(1).strip())

for case in ('iq3-stream-plain-1', 'iq3-stream-mmap-16tokens-1',
             'probe-iq3-gpu-chinese-system-32', 'probe-iq3-cpu-chinese-system-32',
             'launcher-chat-iq1-4', 'launcher-chat-iq3-32'):
    describe(case)

def tokens(label):
    text = (HERE / (label + '.log')).read_text(encoding='utf-8', errors='replace')
    return [int(x) for x in re.findall(r'generation_step=\d+ token_id=(\d+)', text)]

cpu = tokens('probe-iq3-cpu-chinese-system-32')
gpu = tokens('probe-iq3-gpu-chinese-system-32')
common = next((i for i, pair in enumerate(zip(cpu, gpu)) if pair[0] != pair[1]), min(len(cpu), len(gpu)))
print('Chinese CPU/GPU identical initial token count:', common)

stream = HERE / 'probe-iq1-gpu-capital-raw.bin'
resident = HERE.parent / 'large-matvec/full-model/capital-enabled1-trial1.bin'
if stream.exists() and resident.exists():
    print('IQ1 first-position full-vocabulary binary identical:', stream.read_bytes() == resident.read_bytes())

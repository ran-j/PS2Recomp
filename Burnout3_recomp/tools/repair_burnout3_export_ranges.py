"""Bound exported ranges to saved Ghidra instructions, without running recompilation.

The repository exporter encloses noncontiguous function bodies and extends some
synthetic labels to the next label, even across data. Intersect its ranges with
the instruction runs exported by ExportBurnout3InstructionRanges.java. Preserve
every original entry and every instruction covered by the original map; assign
entry_ names to additional fragments so branches can still reach them.
"""
from bisect import bisect_right
from pathlib import Path
import csv
import hashlib
import json
import re
import struct
import tomllib

workspace = Path(__file__).resolve().parents[1]
repo = workspace.parent
# Machine-specific values live in the git-ignored local/ directory.
local = json.loads((workspace / 'local/burnout3_local.json').read_text())

def rel(path):
    """Repo-relative path, so generated TOMLs and reports carry no home directory."""
    return Path(path).resolve().relative_to(repo).as_posix()
source_csv = workspace / 'backups/before_range_fix/burnout3_usa_ghidra_functions.csv'
source_toml = workspace / 'backups/before_range_fix/burnout3_usa_recomp.toml'
evidence = workspace / 'reports/burnout3_usa_ghidra_instruction_ranges.csv'
fixed_csv = workspace / 'burnout3_usa_ghidra_functions_instruction_bounded.csv'
fixed_toml = workspace / 'burnout3_usa_recomp_instruction_bounded.toml'

def read_ranges(path):
    with path.open(newline='') as stream:
        return list(csv.DictReader(stream))

def merge(intervals):
    result = []
    for start, end in sorted(intervals):
        if result and start <= result[-1][1]:
            result[-1] = (result[-1][0], max(result[-1][1], end))
        else:
            result.append((start, end))
    return result

runs = [(int(r['Start'], 0), int(r['End'], 0)) for r in read_ranges(evidence)]
assert runs and runs == sorted(runs)
assert all(start % 4 == end % 4 == 0 and start < end for start, end in runs)
assert all(runs[i][1] < runs[i + 1][0] for i in range(len(runs) - 1))
run_starts = [start for start, _ in runs]
rows = read_ranges(source_csv)
originals = {int(r['Start'], 0): r for r in rows}
assert len(originals) == len(rows)
fragments = {}
retained = []
changes = []
for row in rows:
    start, end = int(row['Start'], 0), int(row['End'], 0)
    index = bisect_right(run_starts, start) - 1
    # Fail rather than silently remove an original entry not backed by instructions.
    assert index >= 0 and start < runs[index][1], f'Original entry is not code: {row}'
    pieces = []
    while index < len(runs) and runs[index][0] < end:
        low, high = max(start, runs[index][0]), min(end, runs[index][1])
        if low < high:
            pieces.append((low, high))
            retained.append((low, high))
            fragments[low] = max(fragments.get(low, low), high)
        index += 1
    if pieces != [(start, end)]:
        changes.append({'name': row['Name'], 'start': f'0x{start:08X}',
                        'original_end': f'0x{end:08X}',
                        'instruction_fragments': [[f'0x{a:08X}', f'0x{b:08X}'] for a, b in pieces]})

# Preserve the original extent of every existing entry (bounded to its code run).
# Newly introduced fragments may overlap existing entries; this is supported by
# the recompiler, just like the original function/entry records.
for start, row in originals.items():
    index = bisect_right(run_starts, start) - 1
    fragments[start] = min(int(row['End'], 0), runs[index][1])
assert set(originals).issubset(fragments)
assert merge(fragments.items()) == merge(retained), 'Repair would lose originally exported instructions'
assert all(start < end <= runs[bisect_right(run_starts, start) - 1][1]
           for start, end in fragments.items())

# Validate the exact primary-MMI warning condition using this checkout's decoder.
data = (workspace / 'SLUS_210.50').read_bytes()
assert hashlib.sha256(data).hexdigest() == local['elf_sha256']
header = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
sections = [struct.unpack_from('<IIIIIIIIII', data, header[5] + i * header[10])
            for i in range(header[11])]
constants = {name: int(value, 16) for name, value in re.findall(
    r'\b(MMI\w+)\s*=\s*(0x[0-9A-Fa-f]+)',
    (repo / 'ps2xRecomp/include/ps2recomp/instructions.h').read_text())}
decoder = (repo / 'ps2xRecomp/src/lib/r5900_decoder.cpp').read_text()
decoder = decoder.split('void R5900Decoder::decodeMMI(Instruction &inst) const')[1].split('void R5900Decoder::decodeMMI0')[0]
accepted = {constants[name] for name in re.findall(r'case (MMI\w+):', decoder)}
accepted |= {constants[name] for name in ['MMI_MMI0', 'MMI_MMI1', 'MMI_MMI2', 'MMI_MMI3']}
old_union = merge((start, int(row['End'], 0)) for start, row in originals.items())
new_union = merge(fragments.items())

def warning_addresses(intervals):
    result = []
    for start, end in intervals:
        section = next(s for s in sections if s[2] & 4 and s[3] <= start < end <= s[3] + s[5])
        for address in range(start, end, 4):
            word = struct.unpack_from('<I', data, section[4] + address - section[3])[0]
            if word >> 26 == 0x1C and word & 0x3F not in accepted:
                result.append(address)
    return result

before_warnings = warning_addresses(old_union)
after_warnings = warning_addresses(new_union)
assert before_warnings, 'Expected diagnostic reproduction for original map'
assert not after_warnings, f'Unsupported primary MMI words remain: {after_warnings[:10]}'
assert fragments[0x4B9238] == 0x4B9248
assert not any(start <= 0x4BA254 < end for start, end in new_union)
assert any(start <= header[3] < end for start, end in new_union)

with fixed_csv.open('w', newline='') as stream:
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(['Name', 'Start', 'End', 'Size'])
    for start, end in sorted(fragments.items()):
        name = originals[start]['Name'] if start in originals else f'entry_{start:08x}'
        writer.writerow([name, f'0x{start:08X}', f'0x{end:08X}', end - start])
config_text = source_toml.read_text()
config = tomllib.loads(config_text)
for key, value in [('input', rel(workspace / 'SLUS_210.50')),
                   ('output', rel(workspace / 'output')),
                   ('ghidra_output', rel(fixed_csv))]:
    config_text = config_text.replace(f'{key} = ' + json.dumps(config['general'][key]),
                                      f'{key} = ' + json.dumps(value))
added = len(fragments) - len(rows)
for key, value in [('csv_record_count', len(fragments)),
                   ('code_label_count', config['ghidra_export']['code_label_count'] + added)]:
    config_text = re.sub(rf'(?m)^{key} = \d+$', f'{key} = {value}', config_text)
config_text = ('# Instruction-bounded copy; original exports preserved in backups/before_range_fix.\n'
               '# Paths are relative to the PS2Recomp repo root; run ps2_recomp from there.\n' + config_text)
parsed = tomllib.loads(config_text)
assert parsed['general']['ghidra_output'] == rel(fixed_csv)
fixed_toml.write_text(config_text)

report = {
    'status': 'PASS', 'recompilation_run': False,
    'original_csv': rel(source_csv), 'instruction_evidence': rel(evidence),
    'corrected_csv': rel(fixed_csv), 'corrected_toml': rel(fixed_toml),
    'original_records': len(rows), 'corrected_records': len(fragments),
    'original_entries_preserved': len(originals), 'added_code_fragments': added,
    'changed_original_ranges': len(changes),
    'original_unique_covered_bytes': sum(b - a for a, b in old_union),
    'corrected_unique_instruction_bytes': sum(b - a for a, b in new_union),
    'excluded_non_instruction_bytes': sum(b - a for a, b in old_union) - sum(b - a for a, b in new_union),
    'primary_mmi_warning_addresses_before': len(before_warnings),
    'primary_mmi_warning_addresses_after': len(after_warnings),
    'checks': ['All original entry addresses and names preserved',
               'All instructions covered by the original export are still covered',
               'No corrected range crosses a Ghidra data/undefined gap',
               'Known string at 0x004BA254 is no longer decoded',
               'entry_004b9238 now ends at 0x004B9248',
               'ELF entry point remains covered',
               'No unknown primary MMI encodings in corrected ranges'],
    'limitations': ['Uses the existing Ghidra analysis, including its reported errors.',
                    'Does not establish runtime correctness or game playability.',
                    'Recompiler may discover additional ranges during its own passes; full-run results are not yet verified.'],
    'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in [fixed_csv, fixed_toml, evidence]},
    'range_changes': changes,
}
report_path = workspace / 'reports/06_burnout3_export_range_repair.json'
report_path.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: v for k, v in report.items() if k != 'range_changes'}, indent=2))
print('Full repair report:', report_path)

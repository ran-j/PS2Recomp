"""Validate the Ghidra exports locally without invoking the recompiler."""
from pathlib import Path
from bisect import bisect_right
import csv
import hashlib
import json
import struct
import tomllib

workspace = Path(__file__).resolve().parents[1]
repo = workspace.parent
# Machine-specific values live in the git-ignored local/ directory.
local = json.loads((workspace / 'local/burnout3_local.json').read_text())
original = Path(local['original_elf']).expanduser()
working = workspace / 'SLUS_210.50'
config = workspace / 'burnout3_usa_recomp_instruction_bounded.toml'
function_map = workspace / 'burnout3_usa_ghidra_functions_instruction_bounded.csv'
expected_hash = local['elf_sha256']
checks = []

def resolve(path):
    """TOML paths are relative to the repo root (absolute paths still resolve)."""
    return (repo / path).resolve()

def rel(path):
    return Path(path).resolve().relative_to(repo).as_posix()

def check(condition, message):
    if not condition:
        raise ValueError(message)
    checks.append(message)

data = working.read_bytes()
check(hashlib.sha256(data).hexdigest() == expected_hash, 'Working ELF matches the original verified SHA-256')
check(original.read_bytes() == data, 'Original ELF still exists and matches the working copy')
check(data[:6] == b'\x7fELF\x01\x01', 'ELF is 32-bit little-endian')
header = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
check(header[1] == 8, 'ELF machine is MIPS')
entry = header[3]
sections = []
for index in range(header[11]):
    section = struct.unpack_from('<IIIIIIIIII', data, header[5] + index * header[10])
    if section[2] & 4 and section[5]:
        sections.append((section[3], section[3] + section[5]))
check(bool(sections), 'ELF contains executable sections')

with config.open('rb') as stream:
    parsed = tomllib.load(stream)
general, metadata = parsed['general'], parsed['ghidra_export']
check(resolve(general['input']) == working, 'TOML input points to the working ELF')
check(resolve(general['ghidra_output']) == function_map, 'TOML references the exported CSV')
check(resolve(general['output']) == workspace / 'output', 'Generated output is contained in the workspace')
check(general['patch_syscalls'] is False, 'Exporter retains patch_syscalls = false')
check(general['skip'] == [], 'No functions are configured for skipping')

with function_map.open(newline='') as stream:
    reader = csv.DictReader(stream)
    check(reader.fieldnames == ['Name', 'Start', 'End', 'Size'], 'CSV has the repository exporter schema')
    rows = list(reader)
check(bool(rows), 'CSV is nonempty')
check(len(rows) == metadata['csv_record_count'], 'CSV row count matches export metadata')
check(len(rows) == metadata['function_count'] + metadata['code_label_count'], 'Function and code-label counts sum to CSV row count')
check(metadata['function_count'] > 0, 'Ghidra exported discovered functions')
check(len(general['stubs']) == metadata['stub_count'], 'Stub count matches export metadata')
check(len(general['untracked_stubs']) == metadata['untracked_stub_count'], 'Untracked stub count matches export metadata')
starts = []
ranges = []
outside = []
noncontiguous = 0
for row_number, row in enumerate(rows, 2):
    if None in row or any(value is None for value in row.values()):
        raise ValueError(f'Malformed CSV row {row_number}')
    start, end, size = int(row['Start'], 0), int(row['End'], 0), int(row['Size'])
    if not row['Name'] or not (0 <= start < end <= 0x100000000) or size <= 0:
        raise ValueError(f'Invalid CSV range at row {row_number}')
    if start % 4 or end % 4:
        raise ValueError(f'Unaligned MIPS range at row {row_number}')
    starts.append(start)
    ranges.append((start, end))
    if size != end - start:
        noncontiguous += 1
    if not any(low <= start < high for low, high in sections):
        outside.append(row)
check(starts == sorted(starts), 'CSV records are sorted by address')
check(len(set(starts)) == len(starts), 'CSV start addresses are unique')
check(any(low <= entry < high for low, high in ranges), 'ELF entry point is covered by the CSV')
check(not outside, 'All exported entry addresses are within ELF executable sections')
with (workspace / 'reports/burnout3_usa_ghidra_instruction_ranges.csv').open(newline='') as stream:
    code_runs = [(int(row['Start'], 0), int(row['End'], 0)) for row in csv.DictReader(stream)]
code_starts = [start for start, _ in code_runs]
for start, end in ranges:
    index = bisect_right(code_starts, start) - 1
    if index < 0 or not (code_runs[index][0] <= start < end <= code_runs[index][1]):
        raise ValueError(f'Export crosses a non-instruction gap: {start:08X}-{end:08X}')
check(True, 'Every exported range consists entirely of contiguous Ghidra instructions')
repair_report = json.loads((workspace / 'reports/06_burnout3_export_range_repair.json').read_text())
check(repair_report['primary_mmi_warning_addresses_after'] == 0, 'Range repair reports no unknown primary MMI encodings')
for path in [config, function_map, workspace / 'reports/burnout3_usa_ghidra_instruction_ranges.csv']:
    check(hashlib.sha256(path.read_bytes()).hexdigest() == repair_report['sha256'][path.name],
          f'{path.name} matches the range repair report SHA-256')
check((workspace / 'ghidra_project/Burnout3_USA_Recomp.gpr').exists(), 'Saved Ghidra project exists')
analysis_log = (workspace / 'logs/02_ghidra_analysis_console.log').read_text()
check('Analysis succeeded for file: /SLUS_210.50' in analysis_log, 'Ghidra reported successful auto-analysis')
check('Save succeeded' in analysis_log, 'Ghidra reported successful project save')
export_log = (workspace / 'logs/03_04_ghidra_export_console.log').read_text()
check('Exported TOML config to' in export_log, 'Repository exporter completed')

report = {
    'status': 'PASS',
    'scope': 'Export validation only; does not cover recompiler output.',
    'original_elf': local['original_elf'], 'working_elf': rel(working),
    'elf_size_bytes': len(data), 'elf_sha256': expected_hash,
    'elf_entry_point': f'0x{entry:08X}',
    'config': rel(config), 'function_map': rel(function_map),
    'ghidra_export': metadata,
    'noncontiguous_body_record_count': noncontiguous,
    'checks': checks,
    'limitations': [
        'Validation checks export integrity and consistency, not complete or perfect function recovery.',
        'See the saved analysis report and logs for Ghidra warnings and error bookmarks.',
        'Repository exports were repaired using saved Ghidra instruction ranges; originals were preserved.'
    ],
    'export_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in [config, function_map]},
}
destination = workspace / 'reports/07_burnout3_corrected_export_validation.json'
destination.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))

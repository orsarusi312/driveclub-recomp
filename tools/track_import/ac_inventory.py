#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only inventory of a private Assetto Corsa track folder.

Discovers layout INIs and their FILE references. Does not decode KN5, infer a
physical road, import assets into Blender, or convert physics/AI data.
"""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys


class InventoryError(ValueError):
    pass


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def discover(root):
    """Never follow directory or file symlinks, including links within the root."""
    found, skipped = {}, []
    for directory, dirs, files in os.walk(root, followlinks=False):
        dirs.sort()
        files.sort()
        for name in list(dirs):
            child = Path(directory) / name
            if child.is_symlink():
                skipped.append(child.relative_to(root).as_posix())
                dirs.remove(name)
        for name in files:
            path = Path(directory) / name
            rel = path.relative_to(root).as_posix()
            if path.is_symlink():
                skipped.append(rel)
            elif path.is_file():
                found[rel] = path
                if len(found) > 20000:
                    raise InventoryError('track folder exceeds 20000 files; select a smaller private input folder')
    return found, skipped


def relative_reference(value, ini_name):
    # AC mods are commonly authored on Windows. Convert separators, but never
    # permit drive letters, UNC, rooted references, or traversal in either style.
    value = value.strip().replace('\\', '/')
    relative = PurePosixPath(value)
    if (not value or relative.is_absolute() or ':' in value or
            '..' in relative.parts or any(ord(ch) < 32 for ch in value)):
        raise InventoryError(f'{ini_name}: unsafe model FILE reference {value!r}')
    return value, (PurePosixPath(ini_name).parent / relative).as_posix()


def models_from_ini(name, path, files):
    if path.stat().st_size > 1024 * 1024:
        raise InventoryError(f'{name}: layout INI exceeds 1 MiB')
    raw = path.read_bytes()
    try:
        content, encoding = raw.decode('utf-8-sig'), 'utf-8-sig'
    except UnicodeDecodeError:
        content, encoding = raw.decode('cp1252'), 'cp1252'
    ini = configparser.ConfigParser(interpolation=None, strict=True)
    ini.optionxform = str.upper
    try:
        ini.read_string(content, source=name)
    except configparser.Error as exc:
        raise InventoryError(f'{name}: invalid layout INI: {exc}') from exc
    entries = []
    by_case = {}
    for rel in files:
        by_case.setdefault(rel.casefold(), []).append(rel)
    for section in ini.sections():
        if not re.fullmatch(r'MODEL_\d+', section, re.IGNORECASE):
            continue
        # DEFAULT inheritance must not quietly manufacture a missing FILE entry.
        values = ini._sections[section]
        reference = values.get('FILE', '').strip()
        record = {'section': section, 'file': reference,
                  'placement_raw': {key: values[key] for key in ('POSITION', 'ROTATION') if key in values},
                  'placement_applied': False}
        if not reference:
            record.update(status='missing_file_field', resolved_path=None)
        else:
            normalized, relative = relative_reference(reference, name)
            record['file'] = normalized
            matches = by_case.get(relative.casefold(), [])
            if len(matches) > 1:
                record.update(status='ambiguous_case', resolved_path=None, candidates=matches)
            elif not matches:
                record.update(status='missing', resolved_path=relative)
            elif Path(matches[0]).suffix.lower() != '.kn5':
                record.update(status='unsupported_model_extension', resolved_path=matches[0])
            else:
                record.update(status='present', resolved_path=matches[0],
                              case_mismatch=matches[0] != relative)
        entries.append(record)
    return {'path': name, 'sha256': digest(path), 'encoding': encoding, 'models': entries,
            'status': 'complete' if entries and all(m['status'] == 'present' for m in entries) else 'incomplete'}


def inventory(root, selected=None, licence_note=''):
    root = Path(root).resolve(strict=True)
    if not root.is_dir():
        raise InventoryError('input must be a track directory')
    files, skipped = discover(root)
    layout_names = sorted(name for name in files if re.fullmatch(r'models.*\.ini', Path(name).name, re.IGNORECASE))
    layouts = [models_from_ini(name, files[name], files) for name in layout_names]
    if selected is not None:
        _, selected = relative_reference(selected, 'selected layout')
        if selected not in layout_names:
            raise InventoryError('--models must exactly match one discovered package-relative INI path')
    elif len(layouts) == 1:
        selected = layouts[0]['path']
    chosen = next((layout for layout in layouts if layout['path'] == selected), None)
    kn5 = []
    for name, path in files.items():
        if path.suffix.lower() == '.kn5':
            # Do not claim a version/magic without a decoder supported by source evidence.
            kn5.append({'path': name, 'bytes': path.stat().st_size, 'sha256': digest(path),
                        'format_validated': False, 'embedded_textures': 'not inspected'})
    textures = [name for name in files if Path(name).suffix.lower() in {'.dds', '.png', '.tga', '.jpg', '.jpeg', '.bmp'}]
    references = [name for name in files if Path(name).name.lower() in {'surfaces.ini', 'fast_lane.ai', 'pit_lane.ai', 'ui_track.json'}]
    report = {'schema': 'dc-ac-inventory-1', 'selected_layout': selected, 'layouts': layouts, 'kn5_files': kn5,
              'loose_texture_candidates': textures, 'reference_data_files': references, 'skipped_symlinks': skipped,
              'status': 'inventory_ready' if chosen and chosen['status'] == 'complete' else 'needs_layout_or_dependencies',
              'limitations': ['KN5 geometry, material and embedded texture dependencies are not decoded.',
                              'FILE presence is not proof of a valid KN5 or a correct TC2000 layout.',
                              'POSITION/ROTATION are recorded verbatim; no placement convention is inferred.',
                              'Loose texture names are candidates, not verified material dependencies.',
                              'Assetto Corsa physics and AI files are references only; no conversion performed.']}
    scaffold = {'schema': 'dc-ac-blender-preparation-1', 'status': 'manual_inspection_required',
                'selected_layout': selected,
                'source_models': chosen['models'] if chosen else [],
                'licence': {'note': licence_note, 'permission_verified_by_tool': False,
                            'action': 'Record applicable conversion/redistribution permissions locally before exporting assets.'},
                'blender': {'importer_name': None, 'importer_version': None, 'version': None, 'private_blend': None},
                'mesh_roles': {role: [] for role in ('road_render', 'road_collision', 'walls', 'barriers', 'terrain', 'scenery')},
                'coordinate_review': {'scale_metres_verified': False, 'track_origin': None, 'orientation_verified': False,
                                      'model_placements_verified': False},
                'spawn': {'position_metres': None, 'heading_degrees': None, 'surface_verified': False},
                'next_step': 'Import with a user-supplied compatible KN5 tool, inspect actual geometry and fill exact Blender object names. This scaffold is not a loadable track.json.'}
    return report, scaffold


def write_inventory(root, output, selected=None, licence_note=''):
    root = Path(root).resolve(strict=True)
    output_input = Path(output)
    if output_input.exists() or output_input.is_symlink():
        raise InventoryError('output already exists; use a new directory')
    output = output_input.resolve()
    if output == root or output.is_relative_to(root):
        raise InventoryError('output must be outside the source track folder')
    if output.exists():
        raise InventoryError('output already exists; use a new directory')
    report, scaffold = inventory(root, selected, licence_note)
    # Claim a new output directory atomically; never overwrite the user input or
    # pre-existing output, even if a concurrent invocation has the same -o path.
    output.mkdir(parents=True, exist_ok=False)
    for filename, value in [('inventory.json', report), ('blender-preparation.json', scaffold)]:
        with (output / filename).open('x', encoding='utf-8') as stream:
            json.dump(value, stream, indent=2, ensure_ascii=False)
            stream.write('\n')
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('track_folder')
    parser.add_argument('-o', '--output', required=True, help='NEW directory outside source folder; metadata only')
    parser.add_argument('--models', help='exact package-relative models*.ini path; required to select among several layouts')
    parser.add_argument('--licence-note', default='', help='local licence/permission record, not verified by this tool')
    args = parser.parse_args(argv)
    try:
        report = write_inventory(args.track_folder, args.output, args.models, args.licence_note)
        print(json.dumps({'status': report['status'], 'selected_layout': report['selected_layout'],
                          'layouts': len(report['layouts']), 'kn5_files': len(report['kn5_files'])}, indent=2))
        return 0 if report['status'] == 'inventory_ready' else 2
    except (OSError, ValueError) as exc:
        print(f'ac-inventory: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())

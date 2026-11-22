"""Headless startup observation only; filenames and key NAMES, never values."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time
import winreg
import xml.etree.ElementTree as ET

import psutil

IDENTIFIER = re.compile(r'^[A-Za-z_][A-Za-z0-9_. -]{0,79}$')


def files():
    result = {}
    for scope in ['APPDATA', 'LOCALAPPDATA']:
        root = Path(os.environ[scope])
        for directory, children, names in os.walk(root, followlinks=False):
            children[:] = [name for name in children if not os.path.islink(os.path.join(directory, name))
                           and not os.path.isjunction(os.path.join(directory, name))]
            for name in names:
                path = Path(directory) / name
                try:
                    result[(scope, str(path.relative_to(root)))] = (path.stat().st_mtime_ns, path.stat().st_size)
                except OSError:
                    pass
    return result


def software_roots():
    names = []
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, 'Software') as key:
        for index in range(winreg.QueryInfoKey(key)[0]):
            names.append(winreg.EnumKey(key, index))
    return set(names)


def registry_names(roots):
    found = []
    pending = [('Software\\' + name, 0) for name in roots]
    while pending and len(found) < 300:
        path, depth = pending.pop()
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                subkeys, values, _ = winreg.QueryInfoKey(key)
                names = [winreg.EnumValue(key, i)[0] for i in range(values)]
                found.append({'key': 'HKCU\\' + path, 'value_names': names})
                if depth < 5:
                    pending.extend((path + '\\' + winreg.EnumKey(key, i), depth + 1) for i in range(subkeys))
        except OSError:
            pass
    return found


def key_names(path):
    if path.stat().st_size > 2 * 1024 * 1024 or path.suffix.lower() not in ['.settings', '.xml', '.json', '.ini', '.cfg']:
        return []
    text = path.read_text(encoding='utf-8-sig', errors='replace')
    names = set()
    def add(name):
        if isinstance(name, str) and IDENTIFIER.fullmatch(name):
            names.add(name)
    if text.lstrip().startswith('<'):
        if '<!DOCTYPE' in text.upper():
            return []
        root = ET.fromstring(text)
        for node in root.iter():
            add(node.tag)
            for name in node.attrib:
                add(name)
            if node.tag.lower() in ['value', 'property', 'entry']:
                # JUCE's VALUE name attribute is the property's key, not its
                # stored value. Never inspect val/value/plugin-state strings.
                add(node.attrib.get('name'))
    elif text.lstrip().startswith(('{', '[')):
        def walk(value, depth=0):
            if depth > 12:
                return
            if isinstance(value, dict):
                for name, child in value.items():
                    add(name)
                    walk(child, depth + 1)
            elif isinstance(value, list):
                for child in value[:100]:
                    walk(child, depth + 1)
        walk(json.loads(text))
    else:
        for line in text.splitlines():
            if '=' in line and not line.lstrip().startswith(('#', ';')):
                add(line.split('=', 1)[0].strip())
    return sorted(names)[:300]


def main():
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('Runner only.')
    parser = argparse.ArgumentParser()
    for name in ['exe', 'work', 'output', 'label']:
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    before = files()
    roots_before = software_roots()
    started = time.perf_counter()
    existing_processes = set(psutil.pids())
    with(Path(args.work) / (args.label + '.stdout.log')).open('wb') as out, (Path(args.work) / (args.label + '.stderr.log')).open('wb') as err:
        process = subprocess.Popen([args.exe], stdout=out, stderr=err, cwd=args.work)
        killed = False
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            killed = True
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
            process.wait(timeout=10)
    # Also close a new same-installation process if a GUI launcher detached
    # before its parent could be killed. Never touch pre-existing processes.
    install = str(Path(args.exe).parent).lower() + os.sep
    for child in psutil.process_iter(['exe']):
        if child.pid not in existing_processes and (child.info['exe'] or '').lower().startswith(install):
            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
    after = files()
    changes = []
    for key, stamp in after.items():
        if before.get(key) == stamp:
            continue
        scope, relative = key
        path = Path(os.environ[scope]) / relative
        row = {'file': '%' + scope + '%\\' + relative, 'change': 'created' if key not in before else 'modified', 'key_names': []}
        try:
            row['key_names'] = key_names(path)
        except (OSError, ValueError, ET.ParseError):
            row['key_parse_failed'] = True
        changes.append(row)
    roots_after = software_roots()
    scopes = roots_after - roots_before
    scopes.update(name for name in roots_after if any(token in name.lower().replace(' ', '') for token in ['zsonic', 'zauro', 'multichannel', 'juce']))
    keys = registry_names(scopes)
    observed = {name for row in changes for name in row['key_names']}
    observed.update(name for row in keys for name in row['value_names'])
    data = {'startup_seconds': time.perf_counter() - started, 'terminated_after_five_seconds': killed,
            'exit_code': process.returncode, 'changed_files': changes[:200], 'changed_files_omitted': max(0, len(changes) - 200),
            'registry': keys,
            'audio_device_keys': sorted(name for name in observed if 'device' in name.lower()),
            'channel_count_keys': sorted(name for name in observed if 'channel' in name.lower() or 'chans' in name.lower()),
            'plugin_keys': sorted(name for name in observed if 'plugin' in name.lower() or 'filterstate' in name.lower()),
            'conclusion': 'Key names identify candidates only. No values were exported, no settings were changed, and headless reconfiguration was not verified. A forced shutdown may prevent the app from saving its settings.'}
    Path(args.output).write_text(json.dumps(data, indent=2), encoding='ascii')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

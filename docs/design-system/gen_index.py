"""Writes project/design-system.json (the index) from asset-ids.txt. Run last."""
import json, os, datetime
root = os.path.dirname(os.path.abspath(__file__))
groups = {'Logos': {'name': 'Logos', 'tile': 'l', 'order': [], 'files': {}},
          'Icons': {'name': 'Icons', 'tile': 'xs', 'order': [], 'files': {}}}
for line in open(os.path.join(root, 'asset-ids.txt'), encoding='utf-8'):
    parts = line.split()
    if len(parts) != 5: continue
    group, name, blob, size, mtype = parts
    g = groups[group]
    g['order'].append(name)
    g['files'][name] = {'name': name, 'blob': blob, 'size': int(size), 'type': mtype}
now = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
index = {
    'v': 3, 'layout': 'files',
    'createdOnFiles': {'v': 1, 'at': now},
    'title': 'EqualizerAPO-XT',
    'namespace': 'EAPOXT',
    'libraries': [],
    'sections': {},
    'groups': ['Logos', 'Icons'],
    'assetGroups': groups,
    'blobs': {},
    'docs': {'readme': 'project/README.md', 'sections': []},
    'lastChange': {'by': '115dkk', 'at': now, 'via': 'Claude Code · GitHub · 115dkk/EqualizerAPO-XT@2ba424e7',
                   'note': 'Second pass: 19 component previews measured against the offscreen gallery (1350 shots) and DeviceSelector --skin-shots; the card-row grammar moved into bundle.css'}
}
out = os.path.join(root, 'project', 'design-system.json')
json.dump(index, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
print('wrote', out, 'assets:', {k: len(v['order']) for k, v in groups.items()})

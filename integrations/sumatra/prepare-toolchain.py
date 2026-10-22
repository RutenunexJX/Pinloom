import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import urllib.parse
import urllib.request
import zipfile
import zlib

root = Path(__file__).resolve().parent
manifest = json.loads((root / 'VisualStudio.vsman').read_text(encoding='utf-8-sig'))
ids = [
    'Microsoft.Build', 'Microsoft.Build.Dependencies',
    'Microsoft.VisualStudio.VC.MSBuild.v170.Base',
    'Microsoft.VisualStudio.VC.MSBuild.v170.Base.Resources',
    'Microsoft.VisualStudio.VC.MSBuild.v170.X64',
    'Microsoft.VisualStudio.VC.MSBuild.v170.X64.v143',
    'Microsoft.VisualStudio.VC.MSBuild.v170.X86',
    'Microsoft.VisualStudio.VC.MSBuild.v170.x86.v143',
    'Microsoft.VC.14.44.17.14.Tools.HostX64.TargetX64.base',
    'Microsoft.VC.14.44.17.14.Tools.HostX64.TargetX64.Res.base',
    'Microsoft.VC.14.44.17.14.Props',
    'Microsoft.VC.14.44.17.14.Props.x64',
    'Microsoft.VC.14.44.17.14.CRT.Headers.base',
    'Microsoft.VC.14.44.17.14.CRT.x64.Desktop.base',
    'Microsoft.VC.14.44.17.14.CRT.x64.Store.base',
    'Microsoft.VC.14.44.17.14.CRT.Redist.X64.base',
    'Microsoft.VC.14.44.17.14.ATL.Headers.base',
    'Microsoft.VC.14.44.17.14.ATL.X64.base',
    'Microsoft.VC.14.44.17.14.Props.ATLMFC',
]
cache = root / 'packages'
cache.mkdir(exist_ok=True)
records = []
for package_id in ids:
    package = next(p for p in manifest['packages'] if p['id'] == package_id and p.get('language') in (None, 'neutral', 'en-US'))
    for payload in package['payloads']:
        records.append(dict(payload, package=package_id, version=package['version']))

def acquire(record):
    target = cache / (record['package'] + '.vsix')
    if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest().lower() != record['sha256'].lower():
        with urllib.request.urlopen(record['url'], timeout=120) as response, target.open('wb') as output:
            shutil.copyfileobj(response, output)
    if hashlib.sha256(target.read_bytes()).hexdigest().lower() != record['sha256'].lower():
        raise RuntimeError('Checksum mismatch: ' + record['package'])
    print('verified ' + record['package'], flush=True)
    return target

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    archives = list(pool.map(acquire, records))

destination = (root / 'VS2022').resolve()
destination.mkdir(exist_ok=True)
for archive in archives:
    with zipfile.ZipFile(archive) as package:
        for item in package.infolist():
            name = urllib.parse.unquote(item.filename).replace('\\', '/')
            if not name.startswith('Contents/') or item.is_dir():
                continue
            output = (destination / name[len('Contents/'):]).resolve()
            if not output.is_relative_to(destination):
                raise RuntimeError('Unsafe archive path')
            if output.exists() and output.stat().st_size == item.file_size and zlib.crc32(output.read_bytes()) == item.CRC:
                continue
            output.parent.mkdir(parents=True, exist_ok=True)
            with package.open(item) as source, output.open('wb') as target:
                shutil.copyfileobj(source, target)
(root / 'toolchain-payloads.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
print('extracted ' + str(destination), flush=True)

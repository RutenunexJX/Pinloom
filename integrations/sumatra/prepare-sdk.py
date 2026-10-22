import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request
import zipfile

root = Path(__file__).resolve().parent
version = '10.0.26100.1742'
names = ['microsoft.windows.sdk.cpp', 'microsoft.windows.sdk.cpp.x64', 'microsoft.windows.sdk.buildtools']
def acquire(name):
    url = f'https://api.nuget.org/v3-flatcontainer/{name}/{version}/{name}.{version}.nupkg'
    target = root / 'packages' / f'{name}.{version}.nupkg'
    if not target.exists():
        with urllib.request.urlopen(url, timeout=180) as response, target.open('wb') as output:
            shutil.copyfileobj(response, output)
    with zipfile.ZipFile(target) as package:
        destination = (root / 'WindowsSDK').resolve()
        for item in package.infolist():
            if item.is_dir():
                continue
            output = (destination / item.filename.replace('\\', '/')).resolve()
            if not output.is_relative_to(destination):
                raise RuntimeError('Unsafe SDK path')
            output.parent.mkdir(parents=True, exist_ok=True)
            with package.open(item) as source, output.open('wb') as stream:
                shutil.copyfileobj(source, stream)
    print('extracted ' + name, flush=True)
    return {'package':name,'version':version,'url':url,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()}

with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    results = list(pool.map(acquire, names))
(root / 'sdk-payloads.json').write_text(json.dumps(results, indent=2), encoding='utf-8')

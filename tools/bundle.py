#!/usr/bin/env python3
"""Bundle tracked source only; never credentials, generated firmware or build output."""
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
files = subprocess.check_output(['git','-C',str(ROOT),'ls-files','-z']).decode().split('\0')
if not any(files):
    raise SystemExit('No tracked source. Commit the reviewed source tree first.')
common = ('README.md', 'docs/', 'SECURITY.md', 'CONTRIBUTING.md', 'CHANGELOG.md', 'os/', 'LICENSES/', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'DESIGN.md', 'design/')
groups = {
    'trevos-full': ('',),
    'pomodoist': common + ('apps/pomodoist/', 'examples/pomodoist-core.c', 'examples/README.md'),
    'cal': common + ('apps/cal/', 'examples/cal-model.c', 'examples/README.md'),
}
for name, prefixes in groups.items():
    out=ROOT/'dist'/f'{name}.tar.gz';out.parent.mkdir(exist_ok=True)
    with tarfile.open(out,'w:gz') as archive:
        for rel in files:
            if not rel or not rel.startswith(prefixes): continue
            path=ROOT/rel
            if path.is_symlink() or path.name=='secrets.h' and rel!='sim/shims/secrets.h' or path.suffix in ('.bin','.elf','.log'):
                raise SystemExit(f'Refusing unsafe bundle member: {rel}')
            archive.add(path,arcname=f'{name}/{rel}',recursive=False)
    print(out.relative_to(ROOT))

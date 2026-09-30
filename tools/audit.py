#!/usr/bin/env python3
"""Reject common accidental private artifacts in the tracked public tree.

This is a release guard, not a substitute for reviewing every diff and image.
"""
from pathlib import Path
import re
import subprocess

root=Path(__file__).resolve().parents[1]
paths=subprocess.check_output(['git','-C',str(root),'ls-files','-z']).decode().split('\0')
patterns = [
    re.compile(r'(?m)^\s*#define\s+(?:WIFI_PASS|WIFI_SSID|TODOIST_TOKEN|GCAL_CLIENT_SECRET|GCAL_REFRESH_TOKEN)\s+"[^"\n]+"'),
    re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
    re.compile(r'\b(?:ghp_|github_pat_)[A-Za-z0-9_]{25,}\b'),
    re.compile(r'\bAIza[A-Za-z0-9_-]{30,}\b'),
    re.compile(r'(?:/Users/|/home/)[A-Za-z0-9_.-]+/'),
    re.compile(r'\b(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\b'),
]
errors=[]
for rel in paths:
    if not rel: continue
    path=root/rel
    if path.is_symlink(): errors.append((rel,'symlink'));continue
    if (path.name=='secrets.h' and rel!='sim/shims/secrets.h') or path.suffix in ('.bin','.elf','.log') or 'managed_components' in path.parts or any(p.startswith('build') for p in path.parts[len(root.parts):]):
        errors.append((rel,'private/generated artifact'));continue
    try: text=path.read_text()
    except UnicodeDecodeError: continue  # images require visual review
    if any(pattern.search(text) for pattern in patterns): errors.append((rel,'credential, machine path or device identifier pattern'))
if errors:
    for rel,reason in errors: print(f'REJECT {rel}: {reason}')
    raise SystemExit(1)
print('PASS: tracked-source privacy guard (images still require visual review)')

#!/usr/bin/env python3
"""Manual, explicit-port S3 flash. Never targets the audio ESP32 co-processor."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',required=True)
    ap.add_argument('--build',type=Path,default=Path('build-release'))
    args=ap.parse_args()
    build=args.build.resolve()
    config=json.loads((build/'flasher_args.json').read_text())
    if config.get('extra_esptool_args',{}).get('chip') != 'esp32s3':
        raise SystemExit('Refusing: build is not for ESP32-S3.')
    # Explicit --chip performs ROM detection and rejects a non-S3 before any write.
    subprocess.run([sys.executable,'-m','esptool','--chip','esp32s3','--port',args.port,'chip_id'],check=True)
    answer=input('Flash this ESP32-S3 with the selected build? Type FLASH: ')
    if answer != 'FLASH': raise SystemExit('Cancelled.')
    subprocess.run([sys.executable,'-m','esptool','--chip','esp32s3','--port',args.port,'write_flash','@flash_args'],cwd=build,check=True)

if __name__=='__main__': main()

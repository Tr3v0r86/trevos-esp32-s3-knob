#!/usr/bin/env python3
"""Run portable logic tests. Set CJSON_DIR or activate ESP-IDF first."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
CJSON = Path(os.environ.get('CJSON_DIR', str(Path(os.environ.get('IDF_PATH', '/nonexistent')) / 'components/json/cJSON')))
if not (CJSON / 'cJSON.c').is_file():
    CJSON = ROOT / 'sim/build/_deps/cjson_src-src'
if not (CJSON / 'cJSON.c').is_file():
    raise SystemExit('Set CJSON_DIR to cJSON source, activate ESP-IDF, or configure sim/build first.')
inc = ['apps/pomodoist/core/include', 'apps/pomodoist/sync/include', 'apps/cal/cal_core', 'os/trevos/include', 'os/trev_net/include', 'sim/shims', 'boards/esp32-s3-knob/main', str(CJSON)]
cal = ['apps/cal/cal_core/cal_model.c', str(CJSON / 'cJSON.c')]
nvs = ['sim/stubs/sim_nvs.c']
cases = [
 ('pomo', ['apps/pomodoist/core/test_pomodoist_core.c','apps/pomodoist/core/pomodoist_core.c'], []),
 ('parse', ['apps/pomodoist/sync/test_pomodoist_parse.c','apps/pomodoist/sync/pomodoist_parse.c','apps/pomodoist/core/pomodoist_core.c',str(CJSON/'cJSON.c')], []),
 ('outbox', ['apps/pomodoist/sync/test_pomodoist_outbox.c','apps/pomodoist/sync/pomodoist_outbox.c']+nvs, []),
 ('cal_model', ['apps/cal/cal_core/test_cal_model.c']+cal, ['apps/cal/cal_core/sample.json']),
 ('cal_google', ['apps/cal/cal_core/test_cal_google.c','apps/cal/cal_core/cal_google.c']+cal, []),
 ('cal_glance', ['apps/cal/cal_core/test_cal_glance.c','apps/cal/cal_core/cal_glance.c']+cal, []),
 ('cal_timeline', ['apps/cal/cal_ui/test_cal_timeline.c'], []),
 ('prefs', ['os/trevos/test_trev_prefs.c','os/trevos/trev_prefs.c']+nvs, []),
 ('ring', ['os/trevos/test_trev_ring.c','os/trevos/ui/trev_ring_geom.c'], []),
 ('wake', ['os/trevos/test_trev_wake.c'], []),
 ('bar', ['os/trevos/test_trev_bar2.c'], []),
 ('net', ['os/trev_net/test_trev_net_policy.c','os/trev_net/trev_net_policy.c'], []),
 ('example_pomo', ['examples/pomodoist-core.c','apps/pomodoist/core/pomodoist_core.c'], []),
 ('example_cal', ['examples/cal-model.c']+cal, []),
 ('wheel', ['boards/esp32-s3-knob/main/test_wheel_decode.c'], []),
]
with tempfile.TemporaryDirectory(prefix='trevos-tests-') as folder:
    for name, sources, args in cases:
        exe = str(Path(folder)/name)
        subprocess.run([os.environ.get('CC','cc'), '-std=c99','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror', *['-I'+i for i in inc],*sources,'-lm','-lpthread','-o',exe],check=True)
        subprocess.run([exe,*args],check=True)
print(f'PASS: {len(cases)} host test programs')

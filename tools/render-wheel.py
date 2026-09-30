#!/usr/bin/env python3
"""Sample real LVGL wheel animations. Build the documented synthetic launcher preset first."""
import argparse
import concurrent.futures
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sim', type=Path, default=Path('sim/build/pucksim'),
                        help='Built pucksim executable')
    parser.add_argument('--output', type=Path, default=Path('dist/wheel'),
                        help='New output directory; existing directories are not overwritten')
    args = parser.parse_args()
    sim = args.sim.resolve()
    if not sim.is_file() or not os.access(sim, os.X_OK):
        parser.error('Build the simulator first, or select its executable with --sim.')
    base = args.output.resolve()
    if base.exists():
        parser.error('Output already exists. Choose a new directory with --output.')
    base.mkdir(parents=True)
    frames = base / 'frames'
    frames.mkdir()

    def capture(name, script, delay):
        output = base / (name + '.ppm')
        env = dict(os.environ, SIM_DISP='null', SIM_TICKS='1500', SIM_TICK_MS='10',
                   SIM_SHOT=str(output))
        env.pop('SIM_SHOT_AFTER_MS', None)
        if script:
            env['SIM_SHOT_AFTER_MS'] = str(delay)
        else:
            env['SIM_TICKS'] = '80'
        result = subprocess.run([str(sim)], input=script, text=True, env=env,
                                capture_output=True, check=True, timeout=30)
        (base / (name + '.log')).write_text(result.stderr)
        if script:
            assert f'[sim] shot at +{delay} ms' in result.stderr, 'Unexpected sample time'
        assert output.read_bytes().startswith(b'P6\n360 360\n255\n'), 'Unexpected image format'
        assert '[ring] disc "Pomodoist"' in result.stderr, 'Build the launcher preset first'
        return output

    capture('rest', '', 0)
    jobs = []
    for step in range(1, 4):
        script = 'WAIT 400\nTURN +1\n' * step
        for ms in range(40, 241, 40):
            jobs.append((f'step{step}-{ms}', script, ms))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda job: capture(*job), jobs))
    sequence = ['rest'] * 25
    for step in range(1, 4):
        sequence += [f'step{step}-{ms}' for ms in range(40, 241, 40)]
        sequence += [f'step{step}-240'] * 25
    sequence += ['step3-240'] * 15
    assert len(sequence) == 133
    for index, name in enumerate(sequence):
        shutil.copyfile(base / (name + '.ppm'), frames / f'{index:04d}.ppm')
    print(f'{len(sequence)} real-renderer frames at 25 fps: {len(sequence) / 25:.2f}s')
    print(f'Frames and sample logs: {base}')


if __name__ == '__main__':
    main()

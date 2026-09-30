"""Tiny trust-boundary checks without contacting Google or a device."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
from unittest.mock import patch

def load(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

auth=load('authorize-calendar')
auth.self_test()
assert auth.oauth_error(io.StringIO('{"error":"invalid_grant"}'))=='invalid_grant'
assert auth.oauth_error(io.StringIO('{"error":"token contents are not an error code"}'))==''
with tempfile.TemporaryDirectory() as tmp:
    client=Path(tmp,'client.json')
    client.write_text('{"installed":{"client_id":"synthetic","client_secret":"synthetic"}}')
    template=Path(tmp,'secrets.h.example');template.write_text('#pragma once\n')
    with patch('sys.argv',['authorize-calendar.py','--client',str(client),'--secrets',str(template),'--account','user@example.com']),patch.object(auth,'authorize') as authorize:
        try: auth.main()
        except auth.Failure: pass
        else: raise AssertionError('template accepted as secret destination')
        authorize.assert_not_called()
    tracked=Path(__file__).resolve().parents[1]/'sim/shims/secrets.h'
    with patch('sys.argv',['authorize-calendar.py','--client',str(client),'--secrets',str(tracked),'--account','user@example.com']),patch.object(auth,'authorize') as authorize:
        try: auth.main()
        except auth.Failure: pass
        else: raise AssertionError('tracked public simulator header accepted')
        authorize.assert_not_called()
    private=Path(tmp,'secrets.h');private.write_text('#pragma once\n')
    auth.validate_destination(private)
flash=load('flash')
with tempfile.TemporaryDirectory() as tmp:
    Path(tmp,'flasher_args.json').write_text(json.dumps({'extra_esptool_args':{'chip':'esp32'}}))
    with patch('sys.argv',['flash.py','--port','synthetic','--build',tmp]),patch.object(flash.subprocess,'run') as run:
        try: flash.main()
        except SystemExit as error: assert 'not for ESP32-S3' in str(error)
        else: raise AssertionError('wrong MCU accepted')
        run.assert_not_called()
print('PASS: OAuth error redaction and wrong-target flash refusal')

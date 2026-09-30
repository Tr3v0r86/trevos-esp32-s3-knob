#!/usr/bin/env python3
"""Authorize your Google Calendar with loopback + PKCE; save only to a local header."""
import argparse
import base64
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

SCOPE = 'openid email https://www.googleapis.com/auth/calendar.events.readonly'

def fail(op, cause, fix):
    """Every failure: one stderr line naming what broke and the exact command that recovers. Exit 1.
    Callers pass fixed strings only; never a token, secret, URL or HTTP body."""
    print(f'FAIL {op}: {cause}. Fix: {fix}', file=sys.stderr)
    raise SystemExit(1)

class Failure(RuntimeError):
    def __init__(self, cause, fix):
        super().__init__(cause)
        self.cause, self.fix = cause, fix

def oauth_error(exc):
    """The short OAuth error code (e.g. invalid_grant) from a Google error body, nothing else."""
    try:
        code = json.load(exc).get('error', '')
    except Exception:
        return ''
    return code if isinstance(code, str) and re.fullmatch(r'[a-z_]{1,40}', code) else ''

def request_json(url, fields=None, token=None):
    req = urllib.request.Request(url, data=urllib.parse.urlencode(fields).encode() if fields else None,
                                 headers={'Authorization': 'Bearer ' + token} if token else {})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)

def update_header(old, values):
    """Replace only Google credential defines; preserve local Wi-Fi/Todoist settings."""
    for key, value in values.items():
        line = '#define ' + key + ' ' + json.dumps(value)
        pattern = r'^[ \t]*#define[ \t]+' + re.escape(key) + r'\b[^\n]*'
        old = re.sub(pattern, lambda _: line, old, flags=re.M) if re.search(pattern, old, re.M) else old + '\n' + line + '\n'
    return old

def self_test():
    old = '#define WIFI_SSID "demo"\n#define GCAL_CAL_ID "old"\n'
    got = update_header(old, {'GCAL_CAL_ID': 'primary', 'GCAL_REFRESH_TOKEN': 'synthetic"value'})
    assert '#define WIFI_SSID "demo"' in got
    assert got.count('#define GCAL_CAL_ID') == 1
    assert '#define GCAL_CAL_ID "primary"' in got
    assert 'synthetic\\"value' in got
    assert update_header(got, {'GCAL_CAL_ID': 'primary', 'GCAL_REFRESH_TOKEN': 'synthetic"value'}) == got
    print('PASS authorization header update')

def validate_destination(path):
    if path.name != 'secrets.h' or path.is_symlink() or not path.is_file():
        raise Failure('destination must be an existing regular secrets.h file, not a template or symlink', 'copy the board template to an ignored local secrets.h')
    resolved = path.resolve()
    repo = Path(__file__).resolve().parents[1]
    if resolved == repo / 'sim/shims/secrets.h':
        raise Failure('the simulator header is public source, not a credential destination', 'use boards/esp32-s3-knob/main/secrets.h')
    git_root = next((p for p in resolved.parents if (p / '.git').exists()), None)
    if git_root is not None:
        relative = str(resolved.relative_to(git_root))
        tracked = subprocess.run(['git', '-C', str(git_root), 'ls-files', '--error-unmatch', '--', relative], capture_output=True).returncode
        ignored = subprocess.run(['git', '-C', str(git_root), 'check-ignore', '-q', '--', relative], capture_output=True).returncode
        if tracked != 1 or ignored != 0:
            raise Failure('credential destination must be untracked and ignored by Git', 'choose an ignored secrets.h or a private path outside a Git checkout')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--client', type=Path, required=True, help='Google Desktop OAuth client JSON')
    ap.add_argument('--secrets', type=Path, required=True, help='Existing ignored local secrets.h')
    ap.add_argument('--account', required=True, help='Google account email you intend to authorize')
    ap.add_argument('--calendar', default='primary', help='Calendar ID accessible to that account')
    ap.add_argument('--no-browser', action='store_true')
    args=ap.parse_args()
    rerun=f'python3 tools/authorize-calendar.py --client {shlex.quote(str(args.client))} --secrets {shlex.quote(str(args.secrets))} --account {shlex.quote(args.account)} --calendar {shlex.quote(args.calendar)}'
    try:
        c=json.loads(args.client.read_text())['installed']
        c['client_id'],c['client_secret']
    except (OSError,ValueError,KeyError,TypeError):
        raise Failure('client JSON missing or not a Google Desktop OAuth client',f'save the Desktop client JSON to {shlex.quote(str(args.client))}, then {rerun}')
    validate_destination(args.secrets)
    try:authorize(args,c,rerun)
    except urllib.error.HTTPError as exc:
        code=oauth_error(exc)
        raise Failure(f'Google refused the request (HTTP {exc.code}{", "+code if code else ""})',rerun)

def authorize(args,c,rerun):
    verifier=secrets.token_urlsafe(64);state=secrets.token_urlsafe(32);result={}
    class Callback(BaseHTTPRequestHandler):
        def log_message(self, *args): pass  # callback URL contains an authorization code
        def do_GET(self):
            u=urllib.parse.urlsplit(self.path);q=urllib.parse.parse_qs(u.query)
            if u.path!='/callback' or not secrets.compare_digest(q.get('state',[''])[0],state):
                self.send_error(400,'Invalid callback');return
            result.update(code=q.get('code',[''])[0],error=q.get('error',[''])[0])
            self.send_response(200);self.send_header('Content-Type','text/plain');self.end_headers()
            self.wfile.write(b'Authorization received. You can return to the terminal.')
    server=HTTPServer(('127.0.0.1',0),Callback);server.timeout=1
    redirect=f'http://127.0.0.1:{server.server_port}/callback'
    auth='https://accounts.google.com/o/oauth2/v2/auth?'+urllib.parse.urlencode({
        'client_id':c['client_id'],'redirect_uri':redirect,'response_type':'code','scope':SCOPE,
        'access_type':'offline','prompt':'consent','login_hint':args.account,'state':state,
        'code_challenge':base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b'=').decode(),
        'code_challenge_method':'S256'})
    print('Open this sign-in link on this computer:',flush=True)
    print(auth,flush=True)
    if not args.no_browser:webbrowser.open(auth)
    deadline=time.monotonic()+600
    try:
        while not result and time.monotonic()<deadline:server.handle_request()
    finally:server.server_close()
    if not result.get('code'):raise Failure('authorization cancelled or timed out; no credentials changed',rerun)
    token=request_json('https://oauth2.googleapis.com/token',{'client_id':c['client_id'],
        'client_secret':c['client_secret'],'code':result['code'],'code_verifier':verifier,
        'redirect_uri':redirect,'grant_type':'authorization_code'})
    who=request_json('https://openidconnect.googleapis.com/v1/userinfo',token=token['access_token'])
    if str(who.get('email', '')).casefold()!=args.account.casefold() or not who.get('email_verified'):
        raise Failure('signed in as a different or unverified Google account; no credentials changed',rerun)
    granted=set(token.get('scope','').split())
    allowed={'openid','email','https://www.googleapis.com/auth/userinfo.email','https://www.googleapis.com/auth/calendar.events.readonly'}
    if not granted.issubset(allowed) or 'https://www.googleapis.com/auth/calendar.events.readonly' not in granted:
        raise Failure('Google granted unexpected permissions; no credentials changed',rerun)
    if not token.get('refresh_token'):raise Failure('Google returned no refresh token; no credentials changed',rerun)
    # Verify offline renewal before persisting anything.
    renewed=request_json('https://oauth2.googleapis.com/token',{'client_id':c['client_id'],
        'client_secret':c['client_secret'],'refresh_token':token['refresh_token'],'grant_type':'refresh_token'})
    if not renewed.get('access_token'):raise Failure('token renewal check failed; no credentials changed',rerun)
    values={'GCAL_CLIENT_ID':c['client_id'],'GCAL_CLIENT_SECRET':c['client_secret'],
            'GCAL_REFRESH_TOKEN':token['refresh_token'],'GCAL_CAL_ID':args.calendar}
    old=update_header(args.secrets.read_text(), values)
    tmp=args.secrets.with_name(args.secrets.name+'.oauth-tmp')
    fd=os.open(tmp,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    try:
        with os.fdopen(fd,'w') as f:f.write(old);f.flush();os.fsync(f.fileno())
        os.replace(tmp,args.secrets)
    finally:
        if tmp.exists():tmp.unlink()
    print('Verified the requested account and token renewal. Local credentials saved; rebuild before USB flashing.')

def cli():
    try:main()
    except Failure as exc:fail('authorize',exc.cause,exc.fix)
    except OSError as exc:  # URLError, timeouts, file writes: type only, messages can carry URLs
        fail('authorize',f'{type(exc).__name__} (network or file error)','check the network and that the destination header directory is writable, then rerun the same command')
    except Exception as exc:
        fail('authorize',f'unexpected {type(exc).__name__}','rerun the same command; report the exception type without credential files or callback URLs')

if __name__=='__main__':
    if sys.argv[1:] == ['--self-test']: self_test()
    else: cli()

#!/usr/bin/env python3
"""Run a local command while a USB-connected Android device stays awake and dims.

This is an experimental single-device helper. It does not change brightness
settings, and requires a screen that naturally dims while still interactive.
"""
import argparse
import os
import re
import subprocess
import sys
import time

parser=argparse.ArgumentParser()
parser.add_argument('--adb',default='adb',help='path to adb')
parser.add_argument('--serial',help='optional adb device serial')
parser.add_argument('--dim-timeout',type=int,default=60)
parser.add_argument('command',nargs=argparse.REMAINDER,help='local command after --')
args=parser.parse_args()
command=args.command[1:] if args.command and args.command[0]=='--' else args.command
if not command:parser.error('a local command is required after --')
if not 10<=args.dim_timeout<=180:parser.error('dim timeout must be between 10 and 180 seconds')
adb=[args.adb]+(['-s',args.serial] if args.serial else [])
def shell(*argv):
    return subprocess.check_output(adb+['shell',*argv],timeout=12).decode(errors='replace')
def state():
    p=shell('dumpsys','power');d=shell('dumpsys','display')
    def match(text,pattern):
        m=re.search(pattern,text,re.M)
        if not m:raise RuntimeError('power/display state unavailable')
        return m.group(1)
    return {'wakefulness':match(p,r'^\s*mWakefulness=(\S+)'),
            'stay_on':match(p,r'^\s*mStayOn=(\S+)'),
            'brightness':float(match(d,r'^\s*mScreenBrightness=([0-9.]+)'))}
saved=shell('settings','get','global','stay_on_while_plugged_in').strip()
if saved!='null' and not saved.isdigit():raise RuntimeError('unexpected original stay-on setting')
initial=state();child=None;bad_state=False
try:
    shell('svc','power','stayon','usb')
    shell('input','keyevent','KEYCODE_WAKEUP')
    deadline=time.monotonic()+args.dim_timeout
    while True:
        s=state()
        if s['wakefulness']=='Awake' and s['stay_on']=='true' and 0<s['brightness']<0.01:break
        if time.monotonic()>deadline:raise RuntimeError('device did not remain awake at minimum brightness; command not started')
        time.sleep(3)
    print('Device awake; display reports minimum brightness. Starting command.',flush=True)
    child_env=os.environ.copy()
    if args.serial:child_env['ANDROID_SERIAL']=args.serial
    child=subprocess.Popen(command,env=child_env)
    while child.poll() is None:
        time.sleep(5)
        try:
            s=state()
            if s['wakefulness']!='Awake' or s['stay_on']!='true' or not 0<s['brightness']<0.01:
                bad_state=True
        except (subprocess.SubprocessError,RuntimeError,ValueError):bad_state=True
    rc=child.wait()
    if bad_state:print('Warning: display or power state changed during the command.',file=sys.stderr,flush=True)
finally:
    if child is not None and child.poll() is None:
        child.terminate()
        try:child.wait(timeout=10)
        except subprocess.TimeoutExpired:child.kill();child.wait()
    if saved=='null':shell('settings','delete','global','stay_on_while_plugged_in')
    else:shell('settings','put','global','stay_on_while_plugged_in',saved)
    if initial['wakefulness'] in ('Asleep','Dozing'):shell('input','keyevent','KEYCODE_SLEEP')
    restored=shell('settings','get','global','stay_on_while_plugged_in').strip()
    if restored!=saved:raise RuntimeError('could not restore the previous stay-on setting')
    print('Previous stay-on setting restored.',flush=True)
sys.exit(rc if rc else (3 if bad_state else 0))

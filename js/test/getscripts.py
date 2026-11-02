#!/usr/bin/env python3
"""Runs a page in pagetest until it misses no more external scripts, fetching each missing one
(curl) into the scripts folder: python3 getscripts.py pagetest page.html url scripts-dir
Prints the last run's summary; the page after its scripts is in page.html.out."""
import sys, os, re, subprocess
pt, page, url, d = sys.argv[1:5]
os.makedirs(d, exist_ok=True)
def name(u): return re.sub(r'[^A-Za-z0-9.-]', '_', u)[:240]
tried = set()
for round_ in range(4):
    r = subprocess.run([pt, page, url, d], capture_output=True, env=dict(os.environ, SECONDS=os.environ.get("SECONDS", "40")))
    err = r.stderr.decode('utf-8', 'replace')
    miss = err.split('MISSING:\n', 1)[1].split('\n') if 'MISSING:\n' in err else []
    miss = [m for m in miss if m.startswith('http') and m not in tried]
    if not miss: break
    for m in miss:
        tried.add(m)
        subprocess.run(['curl', '-sSL', '--compressed', '-m', '20', '-A', 'Mozilla/5.0', '-o', os.path.join(d, name(m)), m], capture_output=True)
open(page + '.out', 'wb').write(r.stdout)
print('\n'.join(l for l in err.split('\n') if 'runtime error' not in l and l.strip())[:3000])

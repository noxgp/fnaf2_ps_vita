#!/usr/bin/env python3
"""Build the Vita game data from YOUR OWN copy of the FNaF 2 Android APK.
Recommended: version 2.0.7 (latest). 2.0.6 works too; other versions are built with a warning.

    python build_data.py fnaf2.apk [output_dir]

Creates <output_dir>/game (default: ./fnaf2_data/game). Copy that 'game' folder to
ux0:data/fnaf2/game on your Vita. Optional:  --vpk base.vpk  also writes a single
FNaF2_Vita_full.vpk with the data packed inside (bigger install, nothing to copy).

Needs: Python 3.8+, ffmpeg, and  pip install -r requirements.txt
ffmpeg is found on PATH, next to this script, or with --ffmpeg /path/to/ffmpeg
"""
import os, sys, shutil, subprocess, tempfile, zipfile, argparse, glob, re, hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def die(msg):
    print('error:', msg); sys.exit(1)


LATEST = '2.0.7'
# versions tested end to end with this port; the hash is of res/raw/application.ccn
TESTED = {'2.0.7': '0884b6330e670a51149c1f7209365478', '2.0.6': '8cb388055814164e0a7a8255644ce50b'}


def apk_version(z):
    """versionName from the binary AndroidManifest.xml (best effort, no Android tools needed)"""
    try:
        m = z.read('AndroidManifest.xml')
    except KeyError:
        return None
    for enc, pat in (('utf-16le', rb'(?:[0-9]\x00){1,3}\.\x00(?:[0-9]\x00){1,3}(?:\.\x00(?:[0-9]\x00){1,3}){0,2}'), ('utf-8', rb'[0-9]{1,3}\.[0-9]{1,3}(?:\.[0-9]{1,3}){0,2}')):
        for hit in re.findall(pat, m):
            v = hit.decode(enc, 'ignore')
            if v.count('.') >= 1 and not v.startswith('0.'): return v
    return None


def check_version(z, ccn_bytes):
    ver = apk_version(z) or 'unknown'
    h = hashlib.md5(ccn_bytes).hexdigest()
    print('APK version: %s' % ver)
    if ver in TESTED and TESTED[ver] == h:
        print('  OK, tested version%s.' % (' (latest)' if ver == LATEST else ''))
        if ver != LATEST: print('  Note: %s is the latest supported version, but this one works too.' % LATEST)
    elif ver in TESTED:
        print('  WARNING: version %s, but the game data differs from the tested copy (modified or different build).' % ver)
        print('           It will probably work. If something is broken, report it on GitHub.')
    else:
        print('  WARNING: untested version. Recommended: %s (latest). Tested: %s.' % (LATEST, ', '.join(sorted(TESTED, reverse=True))))
        print('           Building anyway. If something is broken or missing, report it on GitHub with your version number.')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('apk'); ap.add_argument('out', nargs='?', default='fnaf2_data')
    ap.add_argument('--ffmpeg', help='path to ffmpeg (default: next to this script, then PATH)')
    ap.add_argument('--vpk', help='base VPK from the GitHub release; writes FNaF2_Vita_full.vpk with the data inside')
    a = ap.parse_args()

    ff = a.ffmpeg
    if not ff:
        for cand in (os.path.join(HERE, 'ffmpeg.exe'), os.path.join(HERE, 'ffmpeg')):
            if os.path.isfile(cand): ff = cand; break
    ff = ff or shutil.which('ffmpeg')
    if not ff or not (os.path.isfile(ff) or shutil.which(ff)):
        die('ffmpeg not found. Install it (Windows: winget install ffmpeg | macOS: brew install ffmpeg | Linux: sudo apt install ffmpeg),\n'
            '       put ffmpeg next to this script, or pass --ffmpeg /path/to/ffmpeg')
    if not os.path.isfile(a.apk): die('APK not found: %s' % a.apk)
    try:
        import mkpack
    except ImportError as e:
        die('missing Python module (%s). Run: pip install numpy pillow lz4 imagequant' % e)

    z = zipfile.ZipFile(a.apk)
    names = z.namelist()
    ccns = [n for n in names if n.lower().endswith('.ccn')]
    if not ccns: die('no Clickteam game data (.ccn) found in this APK. Is it the FNaF 2 Android APK?')
    src_ccn = 'res/raw/application.ccn' if 'res/raw/application.ccn' in names else ccns[0]
    ccn_bytes = z.read(src_ccn)
    check_version(z, ccn_bytes)
    game = os.path.join(a.out, 'game'); os.makedirs(game, exist_ok=True)
    tmp = tempfile.mkdtemp()
    try:
        ccn = os.path.join(tmp, 'application.ccn')
        with open(ccn, 'wb') as f: f.write(ccn_bytes)

        print('[1/2] converting images (takes a few minutes)...')
        sys.argv = ['mkpack', ccn, os.path.join(game, 'images.pak'), '-', os.path.join(game, 'application.ccn')]
        mkpack.main()

        sounds = sorted(n for n in names if n.startswith('res/raw/s') and n.split('/')[-1][1:5].isdigit())
        if not sounds: print('  WARNING: no sound files found where expected (res/raw/s####). The game will have no sound.')
        print('[2/2] converting %d sounds...' % len(sounds))
        for n in sounds:
            b = n.split('/')[-1]; stem = b.rsplit('.', 1)[0]; dst = os.path.join(game, stem + '.wav')
            if b.endswith('.wav'):
                with open(dst, 'wb') as f: f.write(z.read(n))
            else:
                src = os.path.join(tmp, b)
                with open(src, 'wb') as f: f.write(z.read(n))
                subprocess.run([ff, '-v', 'error', '-y', '-i', src, '-c:a', 'adpcm_ima_wav', dst], check=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print('done: %s' % os.path.abspath(game))
    print('copy the "game" folder to ux0:data/fnaf2/game on your Vita')

    if a.vpk:
        outv = os.path.join(a.out, 'FNaF2_Vita_full.vpk')
        with zipfile.ZipFile(a.vpk) as src, zipfile.ZipFile(outv, 'w', zipfile.ZIP_DEFLATED) as dst:
            for item in src.infolist(): dst.writestr(item, src.read(item.filename))
            for f in sorted(glob.glob(os.path.join(game, '*'))):
                dst.write(f, 'data/' + os.path.basename(f), compress_type=zipfile.ZIP_STORED if f.endswith('.pak') else zipfile.ZIP_DEFLATED)
        print('full VPK: %s (keep it for yourself, do not share it)' % os.path.abspath(outv))


if __name__ == '__main__':
    main()

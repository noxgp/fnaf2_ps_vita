#!/usr/bin/env python3
"""Build the Vita game data from YOUR OWN copy of the FNaF 2 Android APK.

    python build_data.py fnaf2.apk [output_dir]

Creates <output_dir>/game (default: ./fnaf2_data/game). Copy that 'game' folder to
ux0:data/fnaf2/game on your Vita. Optional:  --vpk base.vpk  also writes a single
FNaF2_Vita_full.vpk with the data packed inside (bigger install, nothing to copy).

Needs: Python 3.8+, ffmpeg, and  pip install -r requirements.txt
ffmpeg is found on PATH, next to this script, or with --ffmpeg /path/to/ffmpeg
"""
import os, sys, shutil, subprocess, tempfile, zipfile, argparse, glob

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def die(msg):
    print('error:', msg); sys.exit(1)


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
    if 'res/raw/application.ccn' not in names: die('this is not the FNaF 2 Android APK (res/raw/application.ccn missing)')
    game = os.path.join(a.out, 'game'); os.makedirs(game, exist_ok=True)
    tmp = tempfile.mkdtemp()
    try:
        ccn = os.path.join(tmp, 'application.ccn')
        with open(ccn, 'wb') as f: f.write(z.read('res/raw/application.ccn'))

        print('[1/2] converting images (takes a few minutes)...')
        sys.argv = ['mkpack', ccn, os.path.join(game, 'images.pak'), '-', os.path.join(game, 'application.ccn')]
        mkpack.main()

        sounds = sorted(n for n in names if n.startswith('res/raw/s') and n.split('/')[-1][1:5].isdigit())
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

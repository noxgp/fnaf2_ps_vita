# Analysis-side parser for Clickteam Fusion Android .ccn (build 295). Used to scope the C player.
import struct, re, collections, os, sys
class R:
    def __init__(s, d, o=0): s.d=d; s.o=o
    def u8(s): v=s.d[s.o]; s.o+=1; return v
    def i8(s): v=struct.unpack_from('<b',s.d,s.o)[0]; s.o+=1; return v
    def i16(s): v=struct.unpack_from('<h',s.d,s.o)[0]; s.o+=2; return v
    def u16(s): v=struct.unpack_from('<H',s.d,s.o)[0]; s.o+=2; return v
    def i32(s): v=struct.unpack_from('<i',s.d,s.o)[0]; s.o+=4; return v
    def f32(s): v=struct.unpack_from('<f',s.d,s.o)[0]; s.o+=4; return v
    def f64(s): v=struct.unpack_from('<d',s.d,s.o)[0]; s.o+=8; return v
    def col(s): r,g,b,_=s.d[s.o:s.o+4]; s.o+=4; return (r,g,b)
    def str(s):
        e=s.o
        while s.d[e:e+2]!=b'\0\0': e+=2
        v=s.d[s.o:e].decode('utf-16le','replace'); s.o=e+2; return v
def chunks(d, off, end=None):
    end = end or len(d)
    while off+8<=end:
        cid,fl,sz=struct.unpack_from('<HHI',d,off)
        yield cid, off+8, sz
        off+=8+sz
        if cid==0x7F7F: break
TYPES={-7:'Player',-6:'Keyboard',-5:'Create',-4:'Timer',-3:'Game',-2:'Speaker',-1:'System',0:'QBackdrop',1:'Backdrop',2:'Active',3:'Text',4:'Question',5:'Score',6:'Lives',7:'Counter',8:'RTF',9:'SubApp'}
def load_app(path):
    d=open(path,'rb').read(); app={'d':d,'frames':[],'ois':{}}
    for cid,o,sz in chunks(d,16):
        r=R(d,o)
        if cid==0x2223:
            r.o+=4; app['flags']=r.u16(); app['newflags']=r.u16(); r.o+=4; app['w']=r.i16(); app['h']=r.i16(); r.o+=8+8+64; app['border']=r.col(); app['nframes']=r.i32(); app['fps']=r.i32()
        elif cid==0x2232:
            n=r.u16(); vals=[r.i32() for _ in range(n)]; app['gvals']=vals; app['gtypes']=list(d[r.o:r.o+n])
        elif cid==0x2233:
            n=r.i32(); app['gstrs']=[r.str() for _ in range(n)]
        elif cid==0x2234:
            n=r.u16(); r.o+=2; exts={}
            for _ in range(n):
                p=r.o; size=r.i16()
                if size<0: size=-size
                h=r.i16(); r.o+=12; name=r.str(); exts[h]=name; r.o=p+size
            app['exts']=exts
        elif cid==0x2229:
            n=r.i32(); oi=None; off=r.o
            for _ in range(n):
                while True:
                    cid2,fl2,s2=struct.unpack_from('<HHI',d,off); o2=off+8; off+=8+s2
                    r2=R(d,o2)
                    if cid2==0x4444:
                        oi={'handle':r2.i16(),'type':r2.i16(),'flags':r2.u16()}; r2.o+=2; oi['ink']=r2.i32(); oi['inkparam']=r2.i32(); app['ois'][oi['handle']]=oi
                    elif cid2==0x4445: oi['name']=r2.str()
                    elif cid2==0x4446: oi['off']=o2; oi['size']=s2
                    elif cid2==0x4448: oi['effect']=(o2,s2)
                    elif cid2==0x7F7F: break
        elif cid==0x222B:
            app['frame_handles']=[R(d,o+2*i).i16() for i in range(sz//2)]
        elif cid==0x3333:
            app['frames'].append((o,sz))
        elif cid==0x6668: app['sndbank']=(o,sz)
        elif cid==0x6667: app['fontbank']=(o,sz)
        elif cid==0x2238: app['binfiles']=(o,sz)
    return app
def load_common(app, oi):
    d=app['d']; t=oi['type']; b=oi['off']; r=R(d,b); c={}
    if t==1:
        r.o+=4; c['obstacle']=r.i16(); c['colmode']=r.i16(); c['cx']=r.i32(); c['cy']=r.i32(); c['image']=r.i16(); return c
    if t==0:
        r.o+=4; c['obstacle']=r.i16(); c['colmode']=r.i16(); c['cx']=r.i32(); c['cy']=r.i32(); c['border']=r.i16(); c['bcol']=r.col(); c['shape']=r.i16(); c['fill']=r.i16()
        if c['shape']==1: c['lineflags']=r.i16()
        elif c['fill']==1: c['color1']=r.col()
        elif c['fill']==2: c['color1']=r.col(); c['color2']=r.col(); c['grad']=r.i32()
        elif c['fill']==3: c['image']=r.i16()
        return c
    r.o+=4; oMov=r.i16(); oVal=r.i16(); r.o+=2; oCnt=r.i16(); oData=r.i16(); oExt=r.i16(); c['oeflags']=r.i32()
    c['quals']=[r.i16() for _ in range(8)]; oAnim=r.i16(); r.o+=2; oStr=r.i16(); c['flags2']=r.u16(); c['prefs']=r.u16(); c['ident']=d[r.o:r.o+4]; r.o+=4; c['back']=r.col(); fin=r.i32(); fout=r.i32()
    c['fadein']=fin; c['fadeout']=fout
    if oMov:
        r=R(d,b+oMov); n=r.i32(); mv=[]
        for i in range(n):
            r.o=b+oMov+4+i*16; nameoff=r.i32(); mid=r.i32(); doff=r.i32(); dsz=r.i32()
            r.o=b+oMov+doff; ctrl=r.i16(); mtype=r.i16(); move=r.u8(); opt=r.u8(); r.o+=2; dirs=r.i32()
            mv.append({'type':mtype,'ctrl':ctrl,'moveatstart':move,'dirs':dirs,'data':d[r.o:r.o+dsz-12]})
        c['movements']=mv
    if oVal:
        r=R(d,b+oVal); n=r.u16(); c['values']=[r.i32() for _ in range(n)]
        if c['flags2']&128: c['valflags']=r.i32()
    if oStr:
        r=R(d,b+oStr); n=r.u16(); c['strings']=[r.str() for _ in range(n)]
    if oAnim:
        base=b+oAnim; r=R(d,base); r.o+=2; n=r.i16(); offs=[r.i16() for _ in range(n)]; anims={}
        for i,ao in enumerate(offs):
            if not ao: continue
            ab=base+ao; r=R(d,ab); doffs=[r.i16() for _ in range(32)]; dirs={}
            for di,do in enumerate(doffs):
                if not do: continue
                r=R(d,ab+do); dd={'min':r.u8(),'max':r.u8(),'repeat':r.i16(),'repframe':r.i16()}; nf=r.i16(); dd['frames']=[r.i16() for _ in range(nf)]; dirs[di]=dd
            anims[i]=dirs
        c['anims']=anims
    if oCnt:
        r=R(d,b+oCnt); r.o+=2; c['counter']=(r.i32(),r.i32(),r.i32())
    if oExt:
        r=R(d,b+oExt); sz=r.i32(); r.o+=4; c['extver']=r.i32(); c['extid']=r.i32(); c['extpriv']=r.i32(); c['extdata']=d[r.o:r.o+sz-20]
    if oData:
        base=b+oData; r=R(d,base)
        if t in (3,4):
            r.o+=4; c['tcx']=r.i32(); c['tcy']=r.i32(); n=r.i32(); offs=[r.i32() for _ in range(n)]; tx=[]
            for to in offs:
                r=R(d,base+to); tx.append({'font':r.i16(),'flags':r.u16(),'color':r.col(),'text':r.str()})
            c['texts']=tx
        elif t in (5,6,7):
            r.o+=4; k={'cx':r.i32(),'cy':r.i32(),'player':r.i16(),'disp':r.i16(),'dflags':r.u16(),'font':r.i16()}
            if k['disp'] in (1,4):
                n=r.i16(); k['frames']=[r.i16() for _ in range(n)]
            elif k['disp'] in (2,3,5):
                k['border']=r.i16(); k['bcol']=r.col(); k['shape']=r.i16(); k['fill']=r.i16()
                if k['shape']==1: k['lineflags']=r.i16()
                elif k['fill']==1: k['color1']=r.col()
                elif k['fill']==2: k['color1']=r.col(); k['color2']=r.col(); k['grad']=r.i32()
            c['counters']=k
    return c
def parse_expr(r):
    toks=[]
    while True:
        p=r.o; t=r.i16(); n=r.i16()
        if t==0 and n==0: toks.append((0,0,None)); break
        size=r.u16(); data=None
        if t==-1 and n==0: data=r.i32()
        elif t==-1 and n==3: data=r.str()
        elif t==-1 and n==23: data=struct.unpack_from('<q',r.d,r.o)[0]/4294967296.0; r.o+=8
        elif t==-1 and n in (24,50): r.o+=4; data=r.i16()
        elif t>=2 or t==-7:
            oi=r.u16(); oil=r.i16(); data=(oi,oil)
            if n in (16,19): data=(oi,oil,r.i16())
        toks.append((t,n,data)); r.o=p+size
    return toks
EXPR_PARAMS={15,22,23,27,28,45,46,52,53,54,59,62}
def parse_param(d,o):
    r=R(d,o); size=r.i16(); code=r.i16(); p={'code':code,'size':size}
    if code in EXPR_PARAMS:
        p['cmp']=r.i16(); p['expr']=parse_expr(r)
    elif code==1: p['oilist']=r.i16(); p['oi']=r.u16(); p['type']=r.i16()
    elif code==2: p['timer']=r.i32(); p['loops']=r.i32()
    elif code in (3,4,10,11,12,17,26,31,32,37,43,49,50,57,58,60,61,67): p['v']=r.i16()
    elif code in (5,25,29,34,48,56): p['v']=r.i32()
    elif code in (6,7,35,36): p['snd']=r.i16(); p['sflags']=r.u16(); p['name']=r.str()
    elif code in (9,21):
        p['pos']=parse_position(r); p['hfran']=r.u16(); p['oi']=r.u16()
    elif code==13: p['delay']=r.i32(); p['compteur']=r.i32()
    elif code in (14,44): p['key']=r.u16()
    elif code==16: p['pos']=parse_position(r)
    elif code in (19,72): p['zone']=(r.i16(),r.i16(),r.i16(),r.i16())
    elif code==24: p['color']=r.col()
    elif code==38: p['gflags']=r.u16(); p['gid']=r.u16(); p['name']=r.str()  # group
    elif code==39: p['pointer']=r.i32(); p['gid']=r.i16()
    elif code in (40,41,63,64): p['s']=r.str()
    elif code==42: p['timer']=r.i32(); p['loops']=r.i32(); p['cmp']=r.i16()
    elif code in (47,51): p['v']=(r.i16(),r.i16())
    elif code==55: p['raw']=d[o+4:o+size]
    elif code==68: p['raw']=d[o+4:o+size]
    elif code==69: p['raw']=d[o+4:o+size]
    else: p['raw']=d[o+4:o+size]
    return p, o+size
def parse_position(r):
    return {'parent':r.u16(),'flags':r.u16(),'x':r.i16(),'y':r.i16(),'slope':r.i16(),'angle':r.i16(),'dir':r.i32(),'typeparent':r.i16(),'oilist':r.i16(),'layer':r.i16()}
def parse_event(d,o,is_cond):
    r=R(d,o); size=r.u16(); t=r.i16(); n=r.i16(); e={'type':t,'num':n,'oi':r.u16(),'oilist':r.i16(),'flags':r.u8(),'flags2':r.u8(),'nparams':r.u8(),'deftype':r.u8()}
    if is_cond: e['ident']=r.i16()
    po=r.o; e['params']=[]
    for _ in range(e['nparams']):
        p,po=parse_param(d,po); e['params'].append(p)
    return e, o+size
def load_frame(app, idx):
    d=app['d']; o,sz=app['frames'][idx]; f={'name':'','los':[],'layers':[],'groups':[],'quals':[]}
    for cid,o2,s2 in chunks(d,o,o+sz):
        r=R(d,o2)
        if cid==0x3334: f['w']=r.i32(); f['h']=r.i32(); f['bg']=r.col(); f['flags']=r.i32()
        elif cid==0x3335: f['name']=r.str()
        elif cid==0x3338:
            n=r.i32()
            for _ in range(n):
                lo={'handle':r.i16(),'oi':r.i16(),'x':r.i32(),'y':r.i32(),'ptype':r.i16(),'value':r.i16(),'layer':r.i16()}; r.o+=2; f['los'].append(lo)
        elif cid==0x3341:
            n=r.i32()
            for _ in range(n):
                f['layers'].append({'opt':r.i32(),'xc':r.f32(),'yc':r.f32(),'nbkd':r.i32(),'first':r.i32(),'name':r.str()})
        elif cid==0x3349: f['effect']=(r.i32(),r.i32(),r.i32())
        elif cid==0x333B: f['fadein']=(o2,s2)
        elif cid==0x333C: f['fadeout']=(o2,s2)
        elif cid==0x333D:
            end=o2+s2
            while r.o<end:
                tag=d[r.o:r.o+4]; r.o+=4
                if tag==b'ER>>':
                    f['maxobj']=r.i16(); f['maxoi']=r.i16(); f['nplayers']=r.i16(); f['ncond']=[r.i16() for _ in range(17)]; nq=r.i16()
                    f['quals']=[(r.u16(),r.i16()) for _ in range(nq)]
                elif tag==b'ERes': r.i32(); f['nevents']=r.i32()
                elif tag==b'ERev':
                    r.i32(); n=r.i32()
                    for _ in range(n):
                        gp=r.o; gsize=r.i16(); g={'ncond':r.u8(),'nact':r.u8(),'flags':r.u16(),'line':r.i16(),'inhibit':r.i32(),'inhibitcpt':r.i32(),'conds':[],'acts':[]}
                        eo=r.o
                        for _ in range(g['ncond']): e,eo=parse_event(d,eo,True); g['conds'].append(e)
                        for _ in range(g['nact']): e,eo=parse_event(d,eo,False); g['acts'].append(e)
                        f['groups'].append(g); r.o=gp-gsize
                elif tag==b'ERop': f['erop']=r.i32()
                elif tag==b'<<ER': break
                else: raise Exception('bad tag %r at %x'%(tag,r.o))
    return f

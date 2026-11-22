import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fusion import *
app=load_app(sys.argv[1]); f=load_frame(app,int(sys.argv[2])); lo=int(sys.argv[3]); hi=int(sys.argv[4])
def tn(t): return TYPES.get(t) or app['exts'].get(t-32,str(t))[:8]
def ex(p):
    if 'expr' in p:
        out=[]
        for t,n,d in p['expr']:
            if t==0 and n==0: break
            if t==-1 and n in(0,3,23): out.append(repr(d))
            elif t==0: out.append({2:'+',4:'-',6:'*',8:'/',10:'%'}.get(n,'op%d'%n))
            elif t>=2: out.append('%s#%s.%d%s'%(tn(t)[:4],d[0] if d else '',n,('[%d]'%d[2]) if d and len(d)>2 else ''))
            else: out.append('%s.%d%s'%(tn(t)[:3],n,'(%s)'%d if d is not None else ''))
        return ('cmp%d:'%p['cmp'] if p.get('cmp') else '')+' '.join(out)
    return ','.join('%s=%s'%(k,v) for k,v in p.items() if k not in('code','size','raw','pos'))[:60]
def ev(e): return '%s%s/%d oi%s f%x:%x [%s]'%('!' if e['flags2']&1 else '',tn(e['type']),e['num'],e['oi'] if e['oi']<60000 else 'Q%d'%(e['oilist']&0x7fff) if e['oilist']<0 and e['oilist']!=-1 else '-',e['flags'],e['flags2'],' | '.join(ex(p) for p in e['params']))
for i,g in enumerate(f['groups'][lo:hi],lo):
    print('G%d fl%x lv%d'%(i,g['flags'],g['inhibitcpt']))
    for c in g['conds']: print('   C',ev(c)[:190])
    for a in g['acts']: print('   A',ev(a)[:190])

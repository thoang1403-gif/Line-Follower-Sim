"""Export predictions on held-out runs. Loss timestamps remain firmware proxies
until independently checked against observer logs; see docs/VALIDATION.md."""
import argparse,csv,json
from pathlib import Path
from pipeline import rows,FEATURES
import numpy as np
p=argparse.ArgumentParser();p.add_argument('runs');p.add_argument('model_dir');p.add_argument('output');a=p.parse_args()
m=json.loads((Path(a.model_dir)/'model.json').read_text());report=json.loads((Path(a.model_dir)/'evaluation.json').read_text())
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
streams={}
for name,header in [('warnings',['run_id','t_ms']),('losses',['run_id','t_ms']),('coverage',['run_id','start_ms','end_ms'])]:
    f=open(out/(name+'.csv'),'w',newline='');w=csv.writer(f);w.writerow(header);streams[name]=(f,w)
for rid in report['test_runs']:
    run=Path(a.runs)/rid
    for r in rows(run/'features.csv'):
        x=np.array([float(r[k]) for k in FEATURES],dtype=np.float32);i=0
        while m['nodes'][i]['feature']!=-1:
            n=m['nodes'][i];i=n['left'] if x[n['feature']]<=n['threshold'] else n['right']
        if m['nodes'][i]['label']==2:streams['warnings'][1].writerow([rid,r['end_ms']])
    samples=rows(run/'samples.csv');start=prev=None
    for r in samples:
        t=int(r['t_ms'])
        if prev is not None:
            gap=t-int(prev['t_ms'])
            if gap>600:
                streams['coverage'][1].writerow([rid,start,prev['t_ms']]);start=t
            delta=(int(r['loss_count'])-int(prev['loss_count']))%(2**32)
            if delta==1:
                age=(int(r['tick_ms'])-int(r['last_loss_tick']))%(2**32)
                streams['losses'][1].writerow([rid,t-age])
            elif delta>1:raise ValueError(f'{rid}: {delta} losses between packets; missing event timestamps. Do not score partial output; annotate all events independently.')
        else:start=t
        prev=r
    if prev:streams['coverage'][1].writerow([rid,start,prev['t_ms']])
for f,w in streams.values():f.close()

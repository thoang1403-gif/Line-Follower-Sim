"""Synthetic pipeline integration test; results never constitute vehicle accuracy."""
import csv,sys,random,json,subprocess
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'ml'))
from pipeline import FEATURES,train
from evaluate_events import evaluate
root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=True);rng=random.Random(42)
for i in range(12):
    run=root/f'sim_{i}';run.mkdir(exist_ok=True)
    with open(run/'features.csv','w',newline='') as f,open(run/'labels.csv','w',newline='') as l:
        w=csv.writer(f);w.writerow(['run_id','prediction_id','origin','t_ms','start_ms','end_ms','window_ms','deadband','max_gap_ms','model','predicted','suggested']+FEATURES)
        z=csv.writer(l);z.writerow(['run_id','prediction_id','start_ms','end_ms','original','label','utc','kind'])
        for j in range(90):
            k=j//30;label=['stable','oscillating','risk'][k];pid=f'{i}_{j}';t=j*500+3000
            x=[rng.uniform(0,1)+k*3 for _ in FEATURES];x[5]=k*.2;x[-1]=100
            w.writerow([run.name,pid,'simulation',t,t-3000,t,3000,5,600,'fixture','unknown',100]+x)
            z.writerow([run.name,pid,t-3000,t,'unknown',label,'synthetic','fixture'])
train(root,root/'model',True)
r=evaluate([dict(run_id='a',t_ms='1500')],[dict(run_id='a',t_ms='2500'),dict(run_id='a',t_ms='7000')],[dict(run_id='a',start_ms='0',end_ms='10000')])
assert r['detected_events']==1 and r['missed_events']==1 and r['lead_ms']==[1000]
try:train(root,root/'must_reject',False)
except ValueError:pass
else:raise AssertionError('Simulation accepted for real training')
if len(sys.argv)>2:subprocess.run([sys.argv[2],str(root/'model/model.json'),str(root/'model/parity.csv')],check=True)
print('Synthetic training, real-data exclusion, event scoring checks passed')

"""Train a bounded decision tree from GUI features + independently reviewed labels.
No auto-labeling from predictions. Test split is by complete run, never by window.
"""
import argparse, csv, json, hashlib
from pathlib import Path
import numpy as np
from sklearn.tree import DecisionTreeClassifier
from sklearn.model_selection import GroupShuffleSplit
from sklearn.metrics import classification_report, confusion_matrix

FEATURES=['sensor_changes_s','reversals_s','pid_std','pid_delta_mean','pwm_diff_mean','search_fraction','search_entries_s','pid_abs_mean','base_mean']
CLASSES=['stable','oscillating','risk']
def rows(p):
    with open(p,encoding='utf-8-sig',newline='') as f: return list(csv.DictReader(f))
def train(root,out,allow_sim=False):
    data=[]; configs=set(); sources=set()
    for p in Path(root).glob('*/features.csv'):
        labels=p.parent/'labels.csv'
        if not labels.exists(): continue
        reviewed={r['prediction_id']:r['label'] for r in rows(labels)}
        for r in rows(p):
            label=reviewed.get(r['prediction_id'])
            if label not in CLASSES: continue
            if r['origin']!='real' and not allow_sim: continue
            sources.add(r['origin']);configs.add((int(r['window_ms']),float(r['deadband']),int(r['max_gap_ms'])))
            data.append((r,label))
    if len(configs)!=1: raise ValueError('Need reviewed data with one feature configuration')
    groups=np.array([r['run_id'] for r,y in data]); X=np.array([[float(r[k]) for k in FEATURES] for r,y in data]); y=np.array([CLASSES.index(y) for r,y in data])
    if len(set(groups))<6: raise ValueError('Need >=6 independent labeled runs; collect more runs for a credible evaluation')
    if not np.isfinite(X).all(): raise ValueError('Nonfinite feature')
    tr,te=next(GroupShuffleSplit(n_splits=1,test_size=.33,random_state=42).split(X,y,groups))
    if set(y[tr])!={0,1,2} or set(y[te])!={0,1,2}: raise ValueError('All three labels must occur in both run partitions; collect more runs, do not tune the test split')
    model=DecisionTreeClassifier(max_depth=5,min_samples_leaf=5,class_weight='balanced',random_state=42).fit(X[tr],y[tr])
    pred=model.predict(X[te]); tree=model.tree_; nodes=[]
    for i in range(tree.node_count):
        leaf=tree.children_left[i]==-1
        nodes.append(dict(feature=-1 if leaf else int(tree.feature[i]),left=int(tree.children_left[i]),right=int(tree.children_right[i]),label=int(model.classes_[np.argmax(tree.value[i])]) if leaf else -1,threshold=float(tree.threshold[i])))
    window,deadband,gap=next(iter(configs)); src='real' if sources=={'real'} else 'simulation'
    artifact=dict(schema='lf-tree-v1',task='current_state',features=FEATURES,classes=CLASSES,training_source=src,config=dict(window_ms=window,deadband=deadband,max_gap_ms=gap),nodes=nodes)
    artifact['model_id']=hashlib.sha256(json.dumps(artifact,sort_keys=True).encode()).hexdigest()[:16]
    report=dict(training_source=src,train_runs=sorted(set(groups[tr])),test_runs=sorted(set(groups[te])),train_windows=len(tr),test_windows=len(te),confusion_matrix=confusion_matrix(y[te],pred,labels=[0,1,2]).tolist(),classification=classification_report(y[te],pred,labels=[0,1,2],target_names=CLASSES,output_dict=True,zero_division=0),warning='Current-state classification only. No demonstrated early warning or optimum PWM. Simulation is not vehicle validation.')
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    (out/'model.json').write_text(json.dumps(artifact,indent=2),encoding='utf-8');(out/'evaluation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    with open(out/'parity.csv','w',newline='') as f:
        w=csv.writer(f);w.writerow(FEATURES+['expected']);w.writerows([list(x)+[int(p)] for x,p in zip(X,model.predict(X))])
    print(json.dumps(report,indent=2))
def label_interval(run,start,end,label):
    if label not in CLASSES or start>end: raise ValueError('Invalid interval/label')
    import datetime
    path=Path(run)/'labels.csv'
    with open(path,'a',encoding='utf-8',newline='') as f:
        w=csv.writer(f)
        for r in rows(Path(run)/'features.csv'):
            if start<=int(r['start_ms']) and int(r['end_ms'])<=end:
                w.writerow([r['run_id'],r['prediction_id'],r['start_ms'],r['end_ms'],r['predicted'],label,datetime.datetime.now(datetime.timezone.utc).isoformat(),'offline_review'])
if __name__=='__main__':
    p=argparse.ArgumentParser();sub=p.add_subparsers(dest='cmd',required=True)
    t=sub.add_parser('train');t.add_argument('runs');t.add_argument('output');t.add_argument('--allow-simulation',action='store_true')
    l=sub.add_parser('label');l.add_argument('run');l.add_argument('start',type=int);l.add_argument('end',type=int);l.add_argument('label',choices=CLASSES)
    a=p.parse_args()
    if a.cmd=='train':train(a.runs,a.output,a.allow_simulation)
    else:label_interval(a.run,a.start,a.end,a.label)

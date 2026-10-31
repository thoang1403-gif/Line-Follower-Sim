"""Evaluate early-warning episodes against independently checked loss events.
Inputs CSV: warnings(run_id,t_ms), losses(run_id,t_ms), coverage(run_id,start_ms,end_ms).
Use HELD-OUT runs only. Uncovered events are reported, not silently scored as misses.
A warning must be strictly before loss, within horizon, with continuous coverage.
"""
import argparse,csv,json,statistics

def read(p):
    with open(p,newline='',encoding='utf-8-sig') as f:return list(csv.DictReader(f))
def evaluate(warnings,losses,coverage,horizon=2000):
    def covered(run,a,b):return any(c['run_id']==run and int(c['start_ms'])<=a and int(c['end_ms'])>=b for c in coverage)
    alarms=sorted(warnings,key=lambda r:(r['run_id'],int(r['t_ms'])))
    # Collapse sustained risk into alarm episodes (predictions are emitted every 500 ms).
    episodes=[];last={}
    for a in alarms:
        run=a['run_id'];t=int(a['t_ms'])
        if run not in last or t-last[run]>750:episodes.append((run,t))
        last[run]=t
    used=set();leads=[];missed=unscored=0
    for e in sorted(losses,key=lambda r:(r['run_id'],int(r['t_ms']))):
        run=e['run_id'];t=int(e['t_ms'])
        if not covered(run,t-horizon,t):unscored+=1;continue
        candidates=[(i,a) for i,(g,a) in enumerate(episodes) if i not in used and g==run and 0<t-a<=horizon]
        if candidates:
            i,a=candidates[0];used.add(i);leads.append(t-a)
        else:missed+=1
    false=sum(i not in used and covered(g,a,a+horizon) and not any(e['run_id']==g and 0<int(e['t_ms'])-a<=horizon for e in losses) for i,(g,a) in enumerate(episodes))
    return dict(detected_events=len(leads),missed_events=missed,unscored_events=unscored,false_alarm_episodes=false,lead_ms=leads,median_lead_ms=statistics.median(leads) if leads else None,horizon_ms=horizon)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('warnings');p.add_argument('losses');p.add_argument('coverage');p.add_argument('--horizon-ms',type=int,default=2000);a=p.parse_args()
    print(json.dumps(evaluate(read(a.warnings),read(a.losses),read(a.coverage),a.horizon_ms),indent=2))

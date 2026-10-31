#include "controller.h"
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

static QString utc(){return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);}
static QString n(double x){return QString::number(x,'g',16);}
static QStringList sampleFields(const lf::Sample &s){
    return {QString::number(s.boot),QString::number(s.seq),QString::number(s.tick),QString::number(s.t),QString::number(s.received),
    QString::number(s.left),QString::number(s.right),n(s.error),n(s.output),QString::number(s.pwmL),QString::number(s.pwmR),QString::number(s.base),
    QString::number(s.mode),n(s.kp),n(s.ki),n(s.kd),QString::number(s.losses),QString::number(s.lastLoss)};
}
static QStringList sampleHeader(){return {"boot","seq","tick_ms","t_ms","received_ms","left","right","error","pid","pwm_l","pwm_r","base","mode","kp","ki","kd","loss_count","last_loss_tick"};}
Controller::Controller(QObject *parent):QObject(parent),input(this),journal(this) {
    clock.start();storageRoot=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/LineFollowerRuns";
    decoder.sample=[this](lf::Sample s){received(s);};decoder.ack=[this](lf::Ack a){receiveAck(a);};
    input.bytes=[this](const std::string &b){decoder.feed(b);};
    input.disconnected=[this](const QString &why){warning="Mất kết nối: "+why;event("disconnect",why);endRun("disconnect");resetAnalysis();hadSample=false;if(message)message(warning);if(changed)changed();};
    journal.onError=[this](const QString &e){warning=e;autoEnabled=false;event("logging_failed",e);if(message)message(e);if(changed)changed();};
    connect(&monitor,&QTimer::timeout,this,[this]{
        bool f=fresh();
        if(wasFresh&&!f){event("stale","Ngừng gửi đề xuất");resetAnalysis();}
        wasFresh=f;
        if(pendingId&&clock.elapsed()-pendingSince>1800){event("command_timeout","Kết quả áp dụng chưa được xác nhận",pendingDecision);pendingId=0;acked=false;policy.hold(clock.elapsed());autoEnabled=false;warning="Lệnh hết hạn chờ; kiểm tra PWM phản hồi";}
        if(connected()&&f&&clock.elapsed()-lastPing>=500){input.ping();lastPing=clock.elapsed();}
        if(autoEnabled&&(!canAuto()||!f))autoEnabled=false;
        if(changed)changed();
    });monitor.start(200);
}
Controller::~Controller(){endRun("window_closed");input.close();journal.flush();}
bool Controller::connectSource(const QString &port,int baud,QString &error) {
    endRun("reconnect");input.close();decoder.reset();timeline.reset();hadSample=false;wasFresh=false;minOffset=INT64_MAX;warning.clear();resetAnalysis();
    bool ok=input.open(port,baud,error);if(changed)changed();return ok;
}
void Controller::disconnectSource(){event("disconnect","operator");endRun("disconnect");input.close();hadSample=false;wasFresh=false;resetAnalysis();if(changed)changed();}
void Controller::resetAnalysis(){window.clear();decision={};lastInference=-100000;policy.reset();pendingId=0;acked=false;autoEnabled=false;previousPrediction=lf::State::Unknown;}
bool Controller::fresh() const{return connected()&&hadSample&&clock.elapsed()-latest.received<=1000&&latest.received-latest.t-minOffset<=1000;}
QString Controller::engineName() const {
    if(tree.nodes.empty())return "Bộ quy tắc — chưa phải mô hình học máy";
    return QString("ML / %1 — %2").arg(QString::fromStdString(tree.id),tree.realData?"huấn luyện dữ liệu thật; cần kiểm chứng trên xe":"chỉ thử pipeline bằng dữ liệu mô phỏng");
}
QString Controller::quality() const {
    if(!connected())return "Mất/chưa kết nối — không gửi đề xuất";
    if(!hadSample)return "Chưa nhận bản tin hợp lệ";
    if(!fresh())return "Dữ liệu đã cũ (> 1 s) hoặc tồn đọng — không gửi đề xuất";
    if(latest.mode==0)return "Xe đang dừng — không áp dụng PWM";
    if(!decision.features.valid)return QString::fromStdString(decision.prediction.reason.empty()?"Chưa đủ dữ liệu cửa sổ":decision.prediction.reason);
    return pendingId?"Đang chờ ACK và PWM phản hồi":"Dữ liệu mới; kết quả là dự đoán, chưa phải nhãn xác nhận";
}
void Controller::configure(int windowMs,double deadband,int lo,int hi,int step,int intervalMs){
    if(runActive)return;
    window.cfg={std::clamp(windowMs,2000,10000),std::clamp(deadband,0.0,100.0),600};
    if(!tree.nodes.empty())window.cfg=tree.config;
    policy.cfg.minimum=std::clamp(lo,0,255);policy.cfg.maximum=std::clamp(hi,policy.cfg.minimum,255);
    policy.cfg.maxStep=std::clamp(step,1,10);policy.cfg.intervalMs=std::max(3000,intervalMs);resetAnalysis();
}
bool Controller::loadModel(const QString &path,QString &error){
    if(runActive){error="Kết thúc lượt trước khi đổi mô hình";return false;}
    lf::Tree candidate;if(!loadTree(path,candidate,error))return false;tree=std::move(candidate);window.cfg=tree.config;verified=false;resetAnalysis();if(changed)changed();return true;
}
void Controller::useRules(){if(runActive)return;tree={};verified=false;resetAnalysis();if(changed)changed();}
bool Controller::canAuto() const{return verified&&!tree.nodes.empty()&&tree.realData&&hadSample&&!latest.synthetic&&!input.isSimulation()&&journal.ok();}
bool Controller::canApply() const {
    bool same=!decision.samples.empty()&&decision.samples.back().base==latest.base;
    return (tree.nodes.empty()||tree.realData||latest.synthetic)&&runActive&&journal.ok()&&same&&decision.features.valid&&clock.elapsed()-latest.received<=1000&&
        policy.allowed(clock.elapsed(),latest.base,decision.suggested,fresh(),latest.mode,pendingId!=0);
}
bool Controller::apply(){
    if(!canApply())return false;
    pendingId=nextCommand++;pendingTarget=decision.suggested;pendingSince=clock.elapsed();pendingDecision=decision.id;acked=false;
    if(!input.sendPWM(pendingId,pendingTarget)){event("send_failed",QString::number(pendingTarget),decision.id);pendingId=0;return false;}
    policy.sent(clock.elapsed());event("command_sent",QString("id=%1;target=%2;mode=%3").arg(pendingId).arg(pendingTarget).arg(autoEnabled?"auto":"advisory"),decision.id);
    if(changed)changed();return true;
}
void Controller::keep(){event("keep",QString::number(latest.base),decision.id);policy.hold(clock.elapsed());if(changed)changed();}
void Controller::receiveAck(lf::Ack a){
    if(!pendingId||a.id!=pendingId)return;
    event("ack",QString("id=%1;code=%2;base=%3").arg(a.id).arg(a.code).arg(a.base),pendingDecision);
    if(a.code||a.base!=pendingTarget){pendingId=0;acked=false;autoEnabled=false;warning="Arduino từ chối lệnh; xem events.csv";policy.hold(clock.elapsed());}
    else acked=true;
}
void Controller::received(lf::Sample s){
    s.received=clock.elapsed();int accepted=timeline.accept(s);if(!accepted)return;
    if(accepted==2){event("reboot","boot ID changed");endRun("Arduino reboot");resetAnalysis();minOffset=INT64_MAX;hadSample=false;}
    if(hadSample&&s.synthetic!=latest.synthetic){endRun("source kind changed");resetAnalysis();}
    minOffset=std::min(minOffset,s.received-s.t);
    if(hadSample&&(s.base!=latest.base||s.kp!=latest.kp||s.ki!=latest.ki||s.kd!=latest.kd||(s.mode==0)!=(latest.mode==0))) {decision={};lastInference=-100000;policy.observe(lf::State::Unknown);}
    latest=s;hadSample=true;
    if(pendingId&&acked&&s.base==pendingTarget){event("applied_confirmed",QString("id=%1;base=%2").arg(pendingId).arg(s.base),pendingDecision);++applied;pendingId=0;acked=false;}
    window.push(s);
    if(fresh()&&s.t-lastInference>=500){lastInference=s.t;predict();}
    if(!fresh()){decision={};decision.prediction.reason="Dữ liệu tồn đọng";}
    if(runActive)logSample(s);
    if(autoEnabled&&canAuto()&&canApply())apply();
    if(changed)changed();
}
void Controller::predict(){
    decision={};decision.features=window.features();decision.samples=window.samples();decision.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    decision.model=tree.nodes.empty()?"rules-v1":QString::fromStdString(tree.id);
    decision.origin=(latest.synthetic||input.isSimulation())?"simulation":"real";
    if(tree.nodes.empty())decision.prediction=lf::rules(decision.features);
    else {
        decision.prediction.state=tree.predict(decision.features);
        decision.prediction.reason=decision.features.valid?"Mô hình cây phân loại trạng thái; PWM được chọn bằng chính sách giảm có giới hạn":decision.features.reason;
    }
    decision.suggested=policy.propose(latest.base,decision.prediction.state);policy.observe(decision.prediction.state);
    if(runActive&&decision.features.valid){
        QStringList values={runId,decision.id,decision.origin,QString::number(latest.t),QString::number(decision.features.start),QString::number(decision.features.end),
          QString::number(window.cfg.windowMs),n(window.cfg.deadband),"600",decision.model,lf::stateKey(decision.prediction.state),QString::number(decision.suggested)};
        for(double x:decision.features.x)values<<n(x);
        journal.write(runPath+"/features.csv",csv(values).toUtf8());
        if(decision.suggested!=latest.base)event("proposal",QString("base=%1;target=%2;state=%3").arg(latest.base).arg(decision.suggested).arg(lf::stateKey(decision.prediction.state)),decision.id);
    }
}
void Controller::event(const QString &type,const QString &detail,const QString &id){
    if(runActive)journal.write(runPath+"/events.csv",csv({runId,utc(),QString::number(latest.t),type,id,detail}).toUtf8());
    if(message&&type!="proposal")message(type+": "+detail);
}
bool Controller::startRun(const QString &condition){
    if(runActive||!fresh()||condition.trimmed().isEmpty()||!journal.ok())return false;
    runId=QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz")+"_"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    runPath=storageRoot+"/"+runId;runCondition=condition.trimmed();runOrigin=(latest.synthetic||input.isSimulation())?"simulation":"real";
    runStart=clock.elapsed();startedUtc=utc();predictedMs={};observedMs=unknownMs=0;lossEntries=0;applied=feedbackCount=0;pwmHistory={};gainHistory={};hasRunPrevious=false;
    resetAnalysis();runActive=true;previous=latest;hasRunPrevious=true;
    pwmHistory.append(QJsonObject{{"t_ms",double(latest.t)},{"base",latest.base}});
    gainHistory.append(QJsonObject{{"t_ms",double(latest.t)},{"kp",latest.kp},{"ki",latest.ki},{"kd",latest.kd}});
    QJsonObject meta{{"schema","lf-run-v1"},{"run_id",runId},{"origin",runOrigin},{"started_utc",startedUtc},{"condition",runCondition},
     {"model",engineName()},{"window_ms",window.cfg.windowMs},{"deadband",window.cfg.deadband},{"max_gap_ms",window.cfg.maxGapMs},
     {"pwm_min",policy.cfg.minimum},{"pwm_max",policy.cfg.maximum},{"max_step",policy.cfg.maxStep},{"interval_ms",policy.cfg.intervalMs},
     {"initial_kp",latest.kp},{"initial_ki",latest.ki},{"initial_kd",latest.kd},{"initial_base",latest.base}};
    journal.write(runPath+"/metadata.json",QJsonDocument(meta).toJson(),false);
    QStringList sh={"run_id","origin","utc"};sh+=sampleHeader();sh<<"prediction_id"<<"prediction"<<"model"<<"raw_frame";
    journal.write(runPath+"/samples.csv",csv(sh).toUtf8());
    QStringList fh={"run_id","prediction_id","origin","t_ms","start_ms","end_ms","window_ms","deadband","max_gap_ms","model","predicted","suggested"};for(auto k:lf::FeatureNames)fh<<k;
    journal.write(runPath+"/features.csv",csv(fh).toUtf8());
    journal.write(runPath+"/events.csv",csv({"run_id","utc","t_ms","type","prediction_id","detail"}).toUtf8());
    journal.write(runPath+"/labels.csv",csv({"run_id","prediction_id","start_ms","end_ms","original","label","utc","kind"}).toUtf8());
    event("run_start",runCondition);if(changed)changed();return true;
}
void Controller::logSample(const lf::Sample &s){
    if(hasRunPrevious){
        int64_t dt=s.t-previous.t;
        if(dt>0&&dt<=window.cfg.maxGapMs){
            if(previous.mode!=0){observedMs+=dt;int k=int(previousPrediction);if(k>=0&&k<3)predictedMs[k]+=dt;else unknownMs+=dt;}
        }
        auto dl=uint32_t(s.losses-previous.losses);if(dl<0x80000000U)lossEntries+=dl;
    }
    if(!hasRunPrevious||s.base!=previous.base)pwmHistory.append(QJsonObject{{"t_ms",double(s.t)},{"base",s.base}});
    if(!hasRunPrevious||s.kp!=previous.kp||s.ki!=previous.ki||s.kd!=previous.kd)gainHistory.append(QJsonObject{{"t_ms",double(s.t)},{"kp",s.kp},{"ki",s.ki},{"kd",s.kd}});
    QStringList values={runId,runOrigin,utc()};values+=sampleFields(s);values<<decision.id<<lf::stateKey(decision.prediction.state)<<decision.model<<QString::fromStdString(s.wire);
    journal.write(runPath+"/samples.csv",csv(values).toUtf8());
    previous=s;previousPrediction=decision.prediction.state;hasRunPrevious=true;
}
void Controller::endRun(const QString &reason){
    if(!runActive)return;
    event("run_end",reason);
    QJsonObject summary{{"schema","lf-summary-v1"},{"run_id",runId},{"started_utc",startedUtc},{"ended_utc",utc()},{"origin",runOrigin},
     {"condition",runCondition},{"end_reason",reason},{"session_s",(clock.elapsed()-runStart)/1000.0},{"observed_running_s",observedMs/1000.0},
     {"predicted_stable_s",predictedMs[0]/1000.0},{"predicted_oscillating_s",predictedMs[1]/1000.0},{"predicted_risk_s",predictedMs[2]/1000.0},
     {"unclassified_running_s",unknownMs/1000.0},{"loss_entries",double(lossEntries)},{"applied_confirmed",applied},{"feedback_windows",feedbackCount},
     {"pwm_history",pwmHistory},{"gain_history",gainHistory},{"logging_ok_at_close",journal.ok()},
     {"comparison_mode",applied?"assisted":"fixed/no confirmed change"},{"confirmed_duration_s",QJsonValue::Null}};
    journal.write(runPath+"/summary.json",QJsonDocument(summary).toJson(),false);
    runActive=false;autoEnabled=false;pendingId=0;if(runFinished)runFinished(summary);if(changed)changed();
}
void Controller::feedback(const Decision &snap,lf::State label){
    if(!runActive||!snap.features.valid||label==lf::State::Unknown)return;
    journal.write(runPath+"/labels.csv",csv({runId,snap.id,QString::number(snap.features.start),QString::number(snap.features.end),
        lf::stateKey(snap.prediction.state),lf::stateKey(label),utc(),label==snap.prediction.state?"confirm":"correction"}).toUtf8());
    QByteArray data=csv(sampleHeader()).toUtf8();for(const auto &s:snap.samples)data+=csv(sampleFields(s)).toUtf8();
    journal.write(runPath+"/feedback/"+snap.id+".csv",data,false);++feedbackCount;event("feedback",lf::stateKey(label),snap.id);
}
void Controller::confirm(const Decision &snap){feedback(snap,snap.prediction.state);}
void Controller::demoGains(double kp,double ki,double kd){input.demoGains(kp,ki,kd);window.clear();decision={};policy.observe(lf::State::Unknown);event("demo_gains",QString("%1,%2,%3").arg(kp).arg(ki).arg(kd));}

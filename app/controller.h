#pragma once
#include "source.h"
#include "journal.h"
#include "model_io.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>

struct Decision {
    QString id, model, origin;
    lf::Features features;
    lf::Prediction prediction;
    std::deque<lf::Sample> samples;
    int suggested=0;
};
class Controller : public QObject {
    Source input;
    Journal journal;
    lf::Decoder decoder;
    lf::Timeline timeline;
    lf::Window window;
    lf::Policy policy;
    lf::Tree tree;
    QTimer monitor;
    QElapsedTimer clock;
    int64_t lastInference=-100000, minOffset=INT64_MAX, runStart=0, lastPing=0;
    bool hadSample=false, wasFresh=false;
    uint32_t nextCommand=1,pendingId=0;
    int pendingTarget=0;
    int64_t pendingSince=0;
    bool acked=false;
    QString pendingDecision, runPath, runId, runOrigin, runCondition;
    lf::Sample previous;
    lf::State previousPrediction=lf::State::Unknown;
    std::array<double,3> predictedMs{};
    double observedMs=0,unknownMs=0;
    uint64_t lossEntries=0;
    QJsonArray pwmHistory,gainHistory;
    QString startedUtc;
    int applied=0,feedbackCount=0;
    bool runActive=false, hasRunPrevious=false;
    void received(lf::Sample s);
    void receiveAck(lf::Ack a);
    void event(const QString &type,const QString &detail,const QString &decision={});
    void logSample(const lf::Sample &s);
    void predict();
    void resetAnalysis();
public:
    std::function<void()> changed;
    std::function<void(QString)> message;
    std::function<void(QJsonObject)> runFinished;
    bool autoEnabled=false, verified=false;
    lf::Sample latest;
    Decision decision;
    QString storageRoot,warning;
    explicit Controller(QObject *parent=nullptr);
    ~Controller() override;
    Source& source(){return input;}
    bool connectSource(const QString &port,int baud,QString &error);
    void disconnectSource();
    bool connected() const{return input.isOpen();}
    bool fresh() const;
    bool hasSample() const{return hadSample;}
    bool active() const{return runActive;}
    QString currentRun() const{return runId;}
    QString engineName() const;
    QString quality() const;
    uint64_t parseErrors() const{return decoder.errors;}
    uint64_t missingSamples() const{return timeline.missing;}
    lf::FeatureConfig featureConfig() const{return window.cfg;}
    lf::PolicyConfig policyConfig() const{return policy.cfg;}
    void configure(int windowMs,double deadband,int minPWM,int maxPWM,int step,int intervalMs);
    bool loadModel(const QString &path,QString &error);
    void useRules();
    bool canAuto() const;
    bool canApply() const;
    bool apply();
    void keep();
    bool startRun(const QString &condition);
    void endRun(const QString &reason="operator");
    void feedback(const Decision &snapshot,lf::State corrected);
    void confirm(const Decision &snapshot);
    void demoGains(double kp,double ki,double kd);
    void flush(){journal.flush();}
};

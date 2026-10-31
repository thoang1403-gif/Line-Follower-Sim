#pragma once
#include "core/engine.h"
#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <functional>

class Source : public QObject {
    QSerialPort serial;
    QTimer timer;
    bool opened=false, simulation=true;
    lf::Sample s;
    int phase=0, count=0;
    double integral=0, prevError=0;
    int64_t lastCommand=-100000;
public:
    std::function<void(std::string)> bytes;
    std::function<void(QString)> disconnected;
    bool paused=false, automatic=true;
    int manualLeft=0,manualRight=0,scenario=0; // 0 stable, 1 oscillation, 2 risk, 3 S bends, 4 sequence
    explicit Source(QObject *parent=nullptr);
    bool open(const QString &port,int baud,QString &error);
    void close();
    bool isSimulation() const{return simulation;}
    bool isOpen() const{return opened;}
    bool sendPWM(uint32_t id,int target);
    void ping();
    void demoGains(double kp,double ki,double kd);
    void demoBase(int base){if(simulation)s.base=base;}
    void demoRun(bool running){if(simulation)s.mode=running?1:0;}
private:
    void tick();
};

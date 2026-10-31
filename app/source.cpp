#include "source.h"
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
Source::Source(QObject *parent):QObject(parent) {
    connect(&timer,&QTimer::timeout,this,[this]{tick();});
    serial.setReadBufferSize(4096);
    connect(&serial,&QSerialPort::readyRead,this,[this]{auto b=serial.readAll();if(bytes)bytes(b.toStdString());});
    connect(&serial,&QSerialPort::errorOccurred,this,[this](QSerialPort::SerialPortError e){
        if(opened&&e!=QSerialPort::NoError&&e!=QSerialPort::TimeoutError){QString reason=serial.errorString();close();if(disconnected)disconnected(reason);}
    });
}
bool Source::open(const QString &port,int baud,QString &error) {
    close();simulation=port=="DEMO";
    if(simulation){s={};s.boot=QRandomGenerator::global()->generate()|1U;s.base=100;s.kp=30;s.kd=2;s.mode=1;count=phase=0;integral=prevError=0;lastCommand=-100000;opened=true;timer.start(100);return true;}
    serial.setPortName(port);serial.setBaudRate(baud);serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);serial.setStopBits(QSerialPort::OneStop);serial.setFlowControl(QSerialPort::NoFlowControl);
    if(!serial.open(QIODevice::ReadWrite)){error=serial.errorString();return false;}
    opened=true;return true;
}
void Source::close(){opened=false;timer.stop();if(serial.isOpen())serial.close();}
void Source::demoGains(double kp,double ki,double kd){if(simulation){s.kp=kp;s.ki=ki;s.kd=kd;integral=prevError=0;}}
void Source::ping(){if(opened&&!simulation&&serial.bytesToWrite()<256)serial.write(QByteArray::fromStdString(lf::command(0,"PING",0)));}
bool Source::sendPWM(uint32_t id,int target) {
    if(!opened)return false;
    if(!simulation){if(serial.bytesToWrite()>256)return false;auto data=QByteArray::fromStdString(lf::command(id,"PWM",target));return serial.write(data)==data.size();}
    int code=0;
    if(target<20||target>180||std::abs(target-s.base)>10)code=1;
    else if(s.mode!=1)code=2;
    else if(int64_t(s.tick)-lastCommand<3000)code=3;
    if(!code){s.base=target;lastCommand=s.tick;}
    auto wire=lf::frame("A,1,"+std::to_string(id)+","+std::to_string(code)+","+std::to_string(s.base)+",PWM");
    QTimer::singleShot(40,this,[this,wire]{if(opened&&bytes&&!paused)bytes(wire);});return true;
}
void Source::tick() {
    s.tick+=100;++count;
    if(paused)return;
    int kind=scenario==4?(count/80)%4:scenario;
    if(s.mode!=0){
        int mode=(kind==2&&(count%80)>=55)?2:1;
        if(mode==2&&s.mode!=2){++s.losses;s.lastLoss=s.tick;}
        s.mode=mode;
    }
    if(!automatic){s.left=manualLeft;s.right=manualRight;}
    else {
        int e=0;
        if(kind==0)e=(count%30==0)?1:0;
        if(kind==1)e=(count/2)%2?1:-1;
        if(kind==2)e=count%80<50?1:-1;
        if(kind==3)e=(count/20)%2?1:-1;
        s.left=e<0;s.right=e>0;
    }
    s.error=s.right-s.left;
    double derivative=(s.error-prevError)/.1;
    double candidate=integral+s.error*.1;
    double tentative=s.kp*s.error+s.ki*candidate+s.kd*derivative;
    if(std::abs(tentative)<=255||(tentative>255&&s.error<0)||(tentative<-255&&s.error>0))integral=candidate;
    if(s.ki==0)integral=0;
    s.output=s.mode?std::clamp(s.kp*s.error+s.ki*integral+s.kd*derivative,-255.0,255.0):0;
    prevError=s.error;
    s.pwmL=s.mode?std::clamp(int(std::lround(s.base+s.output)),0,255):0;
    s.pwmR=s.mode?std::clamp(int(std::lround(s.base-s.output)),0,255):0;
    ++s.seq;auto wire=lf::encode(s);
    // Exercise the very same stream parser used by HC-05, including split frames.
    if(bytes){auto middle=wire.size()/2;bytes(wire.substr(0,middle));bytes(wire.substr(middle));}
}

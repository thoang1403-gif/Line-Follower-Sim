// Compile the actual firmware against simulated I/O and decode its UART with
// the same protocol parser as the GUI. This does not emulate AVR timing/memory.
#include "../arduino/Control_Hardware/Control_Hardware.ino"
#include "core/engine.h"
#include <iostream>
#include <stdexcept>
#include <vector>

#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)

static void at(uint32_t now){arduino_stub::now=now;loop();}
static void receive(const std::string &wire){
    Serial.rx+=wire;
    while(Serial.available())loop();
}
static void advance(uint32_t end,bool ping=false){
    for(uint32_t now=arduino_stub::now+1;now<=end;++now){
        if(ping&&now%500==0)Serial.rx+=lf::command(0,"PING",0);
        at(now);
    }
}
static void wheels(int left,int right){
    CHECK(arduino_stub::pwm[ENA]==left);
    CHECK(arduino_stub::pwm[ENB]==right);
    CHECK(telemetry.pwmL==left&&telemetry.pwmR==right);
}
static void stopped(){
    wheels(0,0);
    CHECK(telemetry.mode==0&&telemetry.pidMilli==0);
    CHECK(integral==0&&previousError==0&&!hasPreviousError);
    CHECK(arduino_stub::outputs[In1]==LOW&&arduino_stub::outputs[In2]==LOW);
    CHECK(arduino_stub::outputs[In3]==LOW&&arduino_stub::outputs[In4]==LOW);
}
int main(){
    setup();
    CHECK(Serial.baud==9600&&telemetry.boot==1&&EEPROM.writes==1);
    CHECK(LeftSensor==13&&RightSensor==12&&ENA==3&&ENB==11);
    CHECK(In1==5&&In2==6&&In3==9&&In4==10);
    CHECK(arduino_stub::modes[LeftSensor]==INPUT&&arduino_stub::modes[RightSensor]==INPUT);
    for(int pin:{ENA,ENB,In1,In2,In3,In4})CHECK(arduino_stub::modes[pin]==OUTPUT);
    CHECK(Kp==25&&Ki==0&&Kd==0&&basePWM==70);
    advance(9);wheels(0,0);
    advance(2000);wheels(70,70); // Startup is autonomous, no remote session yet.
    CHECK(!telemetryLink.stopRequested);
    CHECK(arduino_stub::outputs[In1]==LOW&&arduino_stub::outputs[In2]==HIGH);
    CHECK(arduino_stub::outputs[In3]==LOW&&arduino_stub::outputs[In4]==HIGH);

    lf::Decoder decoder;
    std::vector<lf::Sample> samples;
    std::vector<lf::Ack> acks;
    decoder.sample=[&](lf::Sample s){samples.push_back(s);};
    decoder.ack=[&](lf::Ack a){acks.push_back(a);};
    auto decode=[&]{decoder.feed(Serial.tx);Serial.tx.clear();CHECK(decoder.errors==0);};
    decode();
    CHECK(samples.size()==9); // Last frame (tick 2000) is still being sent.
    for(size_t i=0;i<samples.size();++i){
        const auto &s=samples[i];
        CHECK(s.tick==(i+1)*200&&s.seq==i+1&&s.boot==1&&!s.synthetic);
        CHECK(s.kp==25&&s.ki==0&&s.kd==0&&s.base==70&&s.mode==1);
        CHECK(s.pwmL==70&&s.pwmR==70&&s.losses==0&&s.lastLoss==0);
    }

    arduino_stub::inputs[RightSensor]=HIGH;
    advance(2009);wheels(70,70); // PID must wait for the next 10 ms deadline.
    advance(2010);wheels(95,45);
    CHECK(telemetry.errorMilli==1000&&telemetry.pidMilli==25000);
    arduino_stub::inputs[LeftSensor]=HIGH;
    arduino_stub::inputs[RightSensor]=LOW;
    advance(2020);wheels(45,95);
    CHECK(telemetry.errorMilli==-1000&&telemetry.pidMilli==-25000);
    arduino_stub::inputs[RightSensor]=HIGH;
    advance(2030);stopped();
    receive(lf::command(1,"PWM",60));
    advance(2040);decode();
    CHECK(acks.back().code==2&&telemetry.base==70);
    CHECK(!telemetryLink.stopRequested);
    arduino_stub::inputs[LeftSensor]=LOW;
    arduino_stub::inputs[RightSensor]=LOW;
    advance(2050);wheels(70,70); // Sensor stop retains its original resumable behavior.

    at(2051);
    const auto command=lf::command(2,"PWM",60);
    receive(command.substr(0,8));CHECK(telemetry.base==70);
    receive(command.substr(8));CHECK(telemetry.base==60);
    CHECK(basePWM==70);wheels(70,70); // Command accepted between control steps.
    advance(2059);CHECK(telemetry.base==60);wheels(70,70);
    advance(2060);CHECK(basePWM==60);wheels(60,60);
    advance(2070);decode();CHECK(acks.back().id==2&&acks.back().code==0);
    receive(command);advance(2080);decode();CHECK(acks.back().code==0); // Idempotent retry.
    receive(lf::command(3,"PWM",50));advance(2090);decode();CHECK(acks.back().code==3);
    receive(lf::command(4,"PWM",40));advance(2100);decode();CHECK(acks.back().code==1);
    receive(lf::command(5,"PWM",181));advance(2110);decode();CHECK(acks.back().code==1);
    auto bad=lf::command(6,"PWM",60);bad[0]='X';
    receive(bad);advance(2120);decode();CHECK(acks.back().id==5);
    CHECK(telemetry.base==60);

    // Reach the minimum through valid commands; with e=+1, u must clamp at 20.
    arduino_stub::inputs[RightSensor]=HIGH;
    uint32_t id=10;
    for(int target:{50,40,30,20}){
        advance(arduino_stub::now+3100,true);
        receive(lf::command(id++,"PWM",target));
        advance(arduino_stub::now+20,true);decode();
        CHECK(acks.back().code==0&&basePWM==target);
    }
    wheels(40,0);CHECK(telemetry.pidMilli==20000);
    advance(arduino_stub::now+250,true);decode();
    CHECK(samples.back().output==20&&samples.back().pwmL==40&&samples.back().pwmR==0);
    CHECK(samples.back().base==20&&samples.back().error==1);

    // UART backpressure must not block control or the watchdog. Stop is handled
    // in the very loop that observes timeout, even if no PID step is due.
    receive(lf::command(0,"PING",0));
    uint32_t heartbeatAt=arduino_stub::now;
    Serial.writable=0;
    advance(heartbeatAt+1500);CHECK(!telemetryLink.stopRequested);wheels(40,0);
    integral=1; // Verify timeout clears all PID state immediately.
    advance(heartbeatAt+1501);CHECK(telemetryLink.stopRequested);stopped();
    Serial.writable=16;
    advance(arduino_stub::now+250);decode();
    CHECK(samples.back().mode==0&&samples.back().pwmL==0&&samples.back().pwmR==0);
    CHECK(samples.back().output==0);
    receive(lf::command(0,"PING",0));
    receive(lf::command(20,"PWM",30));
    advance(arduino_stub::now+250,true);decode();CHECK(acks.back().code==2);
    arduino_stub::inputs[RightSensor]=LOW;
    advance(arduino_stub::now+2000,true);stopped();
    CHECK(telemetryLink.stopRequested&&telemetry.losses==0&&telemetry.lastLoss==0);
    CHECK(EEPROM.writes==1); // No EEPROM writes anywhere in loop().

    // Boot allocation itself, including erased EEPROM and uint32 boundary.
    setup();CHECK(telemetry.boot==2&&EEPROM.writes==2);
    uint32_t boot=0xfffffffeUL;EEPROM.put(0,boot);
    setup();CHECK(telemetry.boot==0xffffffffUL);
    setup();CHECK(telemetry.boot==1);
    for(size_t i=4;i<EEPROM.bytes.size();++i)CHECK(EEPROM.bytes[i]==0xff);
    std::cout<<"Firmware PID, PWM application, real telemetry/GUI parser, EEPROM and latched heartbeat stop passed\n";
}

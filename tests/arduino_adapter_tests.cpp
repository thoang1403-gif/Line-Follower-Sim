#include "TelemetryLink.h"
#include <iostream>
#include <stdexcept>
#include <string>
static std::string frame(std::string b){uint16_t c=0xffff;for(unsigned char x:b){c^=uint16_t(x)<<8;for(int j=0;j<8;++j)c=c&0x8000?uint16_t((c<<1)^0x1021):uint16_t(c<<1);}char suffix[8];snprintf(suffix,sizeof(suffix),"*%04X\n",c);return b+suffix;}
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){HardwareSerial p;Telemetry s;TelemetryLink l(p);s.mode=1;
 for(int i=0;i<500;++i)l.poll(s,i);CHECK(p.tx.find("T,1,")!=std::string::npos);
 p.rx=frame("C,1,1,PWM,90");for(int i=500;i<550;++i)l.poll(s,i);CHECK(s.base==90);
 p.rx=frame("C,1,2,PWM,80");for(int i=550;i<600;++i)l.poll(s,i);CHECK(s.base==90);
 p.rx=frame("C,1,3,PWM,250");for(int i=600;i<650;++i)l.poll(s,i);CHECK(s.base==90);
 for(int i=650;i<2200;++i)l.poll(s,i);CHECK(l.stopRequested);
 CHECK(p.tx.find("A,1,1,0,90,PWM")!=std::string::npos);std::cout<<"Arduino TX, command, rate/range and watchdog tests passed\n";
}

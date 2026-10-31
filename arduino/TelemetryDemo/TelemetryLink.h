#pragma once
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// HardwareSerial only: never use SoftwareSerial as a substitute for nonblocking TX.
struct Telemetry {
 uint32_t boot=1,seq=0,tick=0,losses=0,lastLoss=0;
 int left=0,right=0,pwmL=0,pwmR=0,base=100,mode=0;
 long errorMilli=0,pidMilli=0,kpMilli=30000,kiMilli=0,kdMilli=2000;
 bool synthetic=true;
};
class TelemetryLink {
 HardwareSerial &port;
 char rx[96]={},tx[240]={},ack[70]={};
 uint16_t rn=0,tn=0,pos=0;
 bool dropping=false,remote=false,hadCommand=false;
 uint32_t heartbeat=0,lastAdjust=0,lastEmit=0,lastId=0;
 int lastTarget=0;
 uint16_t period;
 static uint16_t crc(const char *s){uint16_t c=0xffff;while(*s){c^=uint16_t(uint8_t(*s++))<<8;for(int i=0;i<8;++i)c=c&0x8000?uint16_t((c<<1)^0x1021):uint16_t(c<<1);}return c;}
 static void seal(char *b,size_t cap){size_t n=strlen(b);uint16_t c=crc(b);snprintf(b+n,cap-n,"*%04X\n",unsigned(c));}
 void parse(Telemetry &s,uint32_t now){
  char *star=strrchr(rx,'*');if(!star||strlen(star)!=5)return;
  char *end=nullptr;unsigned long checksum=strtoul(star+1,&end,16);if(!end||*end)return;*star=0;if(checksum!=crc(rx))return;
  unsigned long id;int target;char op[8],extra;
  if(sscanf(rx,"C,1,%lu,%7[^,],%d%c",&id,op,&target,&extra)!=3)return;
  if(!strcmp(op,"PING")){heartbeat=now;remote=true;return;}
  if(strcmp(op,"PWM"))return;
  int code=0;
  if(target<20||target>180||abs(target-s.base)>10)code=1;
  else if(s.mode!=1)code=2;
  else if(hadCommand&&id==lastId){code=(target==lastTarget&&s.base==target)?0:4;}
  else if(hadCommand&&uint32_t(now-lastAdjust)<3000)code=3;
  else {s.base=target;lastAdjust=now;lastId=id;lastTarget=target;hadCommand=true;remote=true;heartbeat=now;}
  snprintf(ack,sizeof(ack),"A,1,%lu,%d,%d,PWM",id,code,s.base);seal(ack,sizeof(ack));
 }
public:
 explicit TelemetryLink(HardwareSerial &p,uint16_t periodMs=100):port(p),period(periodMs){}
 // Call often AFTER a due PID step. RX and TX each have a 16-byte per-call budget.
 // Communication loss requests STOP, never increases PWM back to a fallback value.
 bool stopRequested=false;
 void poll(Telemetry &s,uint32_t now){
  for(int budget=0;budget<16&&port.available();++budget){char c=char(port.read());if(c=='\n'){if(!dropping){rx[rn]=0;parse(s,now);}rn=0;dropping=false;}else if(c!='\r'&&!dropping){if(rn+1>=sizeof(rx)){dropping=true;rn=0;}else rx[rn++]=c;}}
  if(remote&&uint32_t(now-heartbeat)>1500){stopRequested=true;remote=false;}
  if(pos>=tn){
   pos=tn=0;
   if(ack[0]){strncpy(tx,ack,sizeof(tx));ack[0]=0;tn=strlen(tx);}
   else if(uint32_t(now-lastEmit)>=period){lastEmit=now;s.tick=now;++s.seq;
    int n=snprintf(tx,sizeof(tx),"T,1,%lu,%lu,%lu,%d,%d,%ld,%ld,%d,%d,%d,%d,%ld,%ld,%ld,%lu,%lu,%d",(unsigned long)s.boot,(unsigned long)s.seq,(unsigned long)s.tick,s.left,s.right,s.errorMilli,s.pidMilli,s.pwmL,s.pwmR,s.base,s.mode,s.kpMilli,s.kiMilli,s.kdMilli,(unsigned long)s.losses,(unsigned long)s.lastLoss,s.synthetic?0:1);
    if(n>0&&n<int(sizeof(tx))-7){seal(tx,sizeof(tx));tn=strlen(tx);}
   }
  }
  int available=port.availableForWrite();int remaining=tn-pos;int n=available<remaining?available:remaining;if(n>16)n=16;
  if(n>0)pos+=port.write(reinterpret_cast<const uint8_t*>(tx+pos),size_t(n));
 }
};

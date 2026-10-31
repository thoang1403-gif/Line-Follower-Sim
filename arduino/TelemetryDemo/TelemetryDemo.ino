#include "TelemetryLink.h"
#include <EEPROM.h>
Telemetry data;
TelemetryLink link(Serial,100);
uint32_t lastControl=0;
void setup(){
 Serial.begin(38400);
 uint32_t boot;EEPROM.get(0,boot);if(boot==0xffffffffUL||boot==0)boot=1;else ++boot;
 EEPROM.put(0,boot);data.boot=boot;data.synthetic=true;data.mode=1;
}
void loop(){
 uint32_t now=millis();
 if(uint32_t(now-lastControl)>=10){lastControl=now;
  // TEST ONLY: no motor pins are touched; these readings are synthetic.
  data.left=(now/500)%2;data.right=1-data.left;
  data.errorMilli=(data.right-data.left)*1000L;data.pidMilli=data.errorMilli*30;
  data.pwmL=constrain(data.base+data.pidMilli/1000,0,255);data.pwmR=constrain(data.base-data.pidMilli/1000,0,255);
  if(link.stopRequested){data.mode=0;data.pwmL=data.pwmR=0;}
 }
 link.poll(data,now);
}

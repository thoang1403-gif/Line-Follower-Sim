#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <array>
#include <stdexcept>
constexpr int LOW=0, HIGH=1, INPUT=0, OUTPUT=1;
namespace arduino_stub {
inline uint32_t now=0;
inline std::array<int,20> inputs{}, outputs{}, pwm{}, modes{};
}
inline unsigned long millis(){return arduino_stub::now;}
inline void pinMode(int pin,int mode){arduino_stub::modes.at(pin)=mode;}
inline int digitalRead(int pin){return arduino_stub::inputs.at(pin);}
inline void digitalWrite(int pin,int value){arduino_stub::outputs.at(pin)=value;}
inline void analogWrite(int pin,int value){arduino_stub::pwm.at(pin)=value;}
template<class T> T constrain(T value,T low,T high){return value<low?low:(value>high?high:value);}
class HardwareSerial {
public:
 std::string rx,tx;
 unsigned long baud=0;
 int writable=16;
 void begin(unsigned long value){baud=value;}
 int available(){return int(rx.size());}
 int read(){char c=rx[0];rx.erase(0,1);return c;}
 int availableForWrite(){return writable;}
 size_t write(const uint8_t*b,size_t n){
  if(n>size_t(writable)||n>16)throw std::runtime_error("UART write exceeded budget");
  tx.append((const char*)b,n);return n;
 }
};
inline HardwareSerial Serial;

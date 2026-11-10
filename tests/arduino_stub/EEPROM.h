#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
class EEPROMClass {
public:
 std::array<uint8_t,1024> bytes;
 unsigned writes=0;
 EEPROMClass(){bytes.fill(0xff);}
 template<class T> void get(int address,T &value){
  if(address!=0||sizeof(T)!=4)throw std::runtime_error("Unexpected EEPROM reservation");
  std::memcpy(&value,bytes.data()+address,sizeof(T));
 }
 template<class T> void put(int address,const T &value){
  if(address!=0||sizeof(T)!=4)throw std::runtime_error("Unexpected EEPROM reservation");
  std::memcpy(bytes.data()+address,&value,sizeof(T));++writes;
 }
};
inline EEPROMClass EEPROM;

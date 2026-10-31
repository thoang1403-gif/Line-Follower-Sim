#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
class HardwareSerial {public:std::string rx,tx;int available(){return int(rx.size());}int read(){char c=rx[0];rx.erase(0,1);return c;}int availableForWrite(){return 16;}size_t write(const uint8_t*b,size_t n){tx.append((const char*)b,n);return n;}};

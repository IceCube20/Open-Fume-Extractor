#pragma once
#include <vector>
class FakeWiFi {
public:
  bool result = true;
  std::vector<int> pins;
  bool setPins(int clk,int cmd,int d0,int d1,int d2,int d3,int rst) {
    pins = {clk,cmd,d0,d1,d2,d3,rst};
    return result;
  }
};
inline FakeWiFi WiFi;

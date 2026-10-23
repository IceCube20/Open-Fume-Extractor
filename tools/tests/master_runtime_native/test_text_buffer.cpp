#include "../../../OpenFumeExtractorMaster/src/OfePsramTextBuffer.h"
#include <cassert>
#include <climits>
#include <limits>
#include <random>
#include <utility>

template <typename Integer> static void number(OfePsramTextBuffer& out,Integer value) {
  char expected[32];
  if constexpr(std::is_signed_v<Integer>)snprintf(expected,sizeof(expected),"%lld",static_cast<long long>(value));
  else snprintf(expected,sizeof(expected),"%llu",static_cast<unsigned long long>(value));
  out.clear();out+=value;
  assert(out.ok()&&out.length()==strlen(expected)&&strcmp(out.c_str(),expected)==0);
}
template <typename Integer> static void limits(OfePsramTextBuffer& out) {
  number(out,std::numeric_limits<Integer>::min());number(out,std::numeric_limits<Integer>::max());
  number(out,Integer(0));number(out,Integer(1));
}
static std::string reference_escape(const char* text) {
  std::string out;
  if(!text)return out;
  for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p) {
    if(*p=='"'||*p=='\\'){out+='\\';out+=char(*p);}
    else if(*p=='\n')out+="\\n";
    else if(*p=='\r')out+="\\r";
    else if(*p=='\t')out+="\\t";
    else if(*p<0x20)out+=' ';
    else out+=char(*p);
  }
  return out;
}
int main() {
  OfePsramTextBuffer out;assert(out.reservePsram(32768)&&out.usesPsram());
  const auto allocations=heap_calls;
  limits<signed char>(out);limits<unsigned char>(out);limits<short>(out);limits<unsigned short>(out);
  limits<int>(out);limits<unsigned>(out);limits<long>(out);limits<unsigned long>(out);
  limits<long long>(out);limits<unsigned long long>(out);
  for(int n=-128;n<128;++n)number(out,static_cast<signed char>(n));
  for(unsigned n=0;n<256;++n)number(out,static_cast<unsigned char>(n));
  std::mt19937_64 rng(1234);
  for(unsigned i=0;i<100000;++i) {
    const auto v=rng();number(out,static_cast<unsigned long long>(v));number(out,static_cast<long long>(v));
    number(out,static_cast<uint32_t>(v));number(out,static_cast<int32_t>(v));
  }
  for(unsigned test=0;test<5000;++test) {
    std::string input;const unsigned length=test%1024;
    for(unsigned i=0;i<length;++i)input+=char(1+rng()%255);
    out.clear();out.appendJsonEscaped(input.c_str());
    assert(out.ok()&&std::string(out.c_str(),out.length())==reference_escape(input.c_str()));
  }
  out.clear();out.appendJsonEscaped(nullptr);assert(out.empty());
  out+='[';out+=INT_MIN;out+=',';out+=ULLONG_MAX;out+=',';out+=0;out+=']';
  assert(strcmp(out.c_str(),"[-2147483648,18446744073709551615,0]")==0);
  out.clear();out+='x';out+=true;out+=1.25;assert(strcmp(out.c_str(),"x11.25")==0);
  assert(heap_calls==allocations&&String::constructions==0); // steady-state no allocations or temporary Strings
  out.clear();out+=String("owned");out+=reinterpret_cast<const __FlashStringHelper*>(" flash");
  assert(strcmp(out.c_str(),"owned flash")==0);
  OfePsramTextBuffer moved(std::move(out));assert(out.empty()&&strcmp(moved.c_str(),"owned flash")==0);
  fail_external=true;OfePsramTextBuffer fallback;assert(fallback.reservePreferred(10000,256)&&!fallback.usesPsram());
  fallback+="prefix";fail_all=true;fallback.appendJsonEscaped(std::string(10000,'"').c_str());
  assert(!fallback.ok()&&strncmp(fallback.c_str(),"prefix",6)==0&&fallback.length()<=fallback.capacity());
  fail_all=false;fallback.clear();fallback+=42;assert(fallback.ok()&&strcmp(fallback.c_str(),"42")==0);
  puts("PASS: real JSON buffer: 400000 decimal differential cases, all integer widths/limits, 5000 escaped byte strings, allocation-free hot path, floats/flash, move and allocation-failure recovery");
}

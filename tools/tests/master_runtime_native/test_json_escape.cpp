#include <Arduino.h>
#include "../../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/JsonTextEscape.h"
#include "generated_json_escape.inc.h"
#include <cassert>
#include <random>

static std::string reference(const char* value) {
  std::string out;
  if(!value)return out;
  for(const unsigned char* p=reinterpret_cast<const unsigned char*>(value);*p;++p) {
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
  assert(json_escape(nullptr).length()==0&&json_escape("").length()==0);
  for(unsigned byte=1;byte<256;++byte) {
    const char text[]={char(byte),0};assert(std::string(json_escape(text).c_str())==reference(text));
  }
  std::mt19937 rng(491);
  for(unsigned test=0;test<5000;++test) {
    std::string input;
    for(unsigned i=0;i<test%1024;++i)input+=char(1+rng()%255);
    assert(std::string(json_escape(input.c_str()).c_str())==reference(input.c_str()));
  }
  const std::string clean(4096,'x');
  String::concat_calls=0;String::reserve_calls=0;
  assert(std::string(json_escape(clean.c_str()).c_str())==clean);
  assert(String::concat_calls==1&&String::reserve_calls==1);
  String::fail_reserve=true;assert(json_escape("not empty").length()==0);String::fail_reserve=false;
  String::fail_concat=true;assert(json_escape("not empty").length()==0);String::fail_concat=false;
  unsigned calls=0;
  assert(!ofe_json::appendEscaped("first\nnext",[&](const char*,size_t){++calls;return false;}));
  assert(calls==1);
  puts("PASS: production JSON escape, every nonzero byte, 5000 differential strings, one concat for 4 KiB clean text, failure handling");
}

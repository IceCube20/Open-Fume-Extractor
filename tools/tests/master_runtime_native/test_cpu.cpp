#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#ifndef configNUMBER_OF_CORES
#define configNUMBER_OF_CORES 2
#endif
#ifndef configGENERATE_RUN_TIME_STATS
#define configGENERATE_RUN_TIME_STATS 1
#endif
#ifdef TEST_COUNTER_64
using configRUN_TIME_COUNTER_TYPE = uint64_t;
#else
using configRUN_TIME_COUNTER_TYPE = uint32_t;
#endif
using BaseType_t = int;
using TaskHandle_t = unsigned;
struct TaskStatus_t { configRUN_TIME_COUNTER_TYPE ulRunTimeCounter; };
static constexpr int pdFALSE=0,eInvalid=0;
static configRUN_TIME_COUNTER_TYPE runtime_now=1000,idle_now[configNUMBER_OF_CORES]={};
static bool idle_missing=false;
static TaskHandle_t cpu_idle_handle_for_core(int core) { return idle_missing ? 0 : core+1; }
static void vTaskGetInfo(TaskHandle_t task,TaskStatus_t* info,int,int) { info->ulRunTimeCounter=idle_now[task-1]; }
static configRUN_TIME_COUNTER_TYPE cpu_runtime_counter_now() { return runtime_now; }
static uint8_t cpu_load_pct=0,cpu_core_load_pct[configNUMBER_OF_CORES]={};
static bool cpu_runtime_valid=false,cpu_core_load_valid=false;
static configRUN_TIME_COUNTER_TYPE cpu_prev_total=0,cpu_prev_idle[configNUMBER_OF_CORES]={};
#define F(x) x
struct String {
  std::string text;
  void operator+=(const char* value) { text+=value; }
  void operator+=(char value) { text+=value; }
  void operator+=(uint8_t value) { text+=std::to_string(value); }
};
#include "generated_cpu.inc.h"
int main() {
  sample_cpu_load();assert(!cpu_core_load_valid);
  String unknown;append_cpu_load_json(unknown);assert(unknown.text.find("[null")!=std::string::npos);
#if configGENERATE_RUN_TIME_STATS == 1
  runtime_now+=1000;
  for(int core=0;core<configNUMBER_OF_CORES;++core)idle_now[core]+=core ? 900 : 100;
  sample_cpu_load();assert(cpu_core_load_valid&&cpu_core_load_pct[0]==90);
  if(configNUMBER_OF_CORES==2)assert(cpu_core_load_pct[1]==10&&cpu_load_pct==50);
  else assert(cpu_load_pct==90);
  String measured;append_cpu_load_json(measured);assert(measured.text.find("[90")!=std::string::npos);
  sample_cpu_load();assert(!cpu_core_load_valid); // no elapsed interval
  runtime_now+=1000;for(auto& idle:idle_now)idle+=1001;
  sample_cpu_load();assert(cpu_core_load_valid&&cpu_load_pct==0);
  for(auto value:cpu_core_load_pct)assert(value==0);
  runtime_now+=1000;sample_cpu_load();assert(cpu_load_pct==100);
  for(auto value:cpu_core_load_pct)assert(value==100);
  idle_missing=true;sample_cpu_load();assert(!cpu_core_load_valid&&!cpu_runtime_valid);
  idle_missing=false;sample_cpu_load();assert(!cpu_core_load_valid);
  // Counter-width-independent modulo subtraction, including both idle counters.
  cpu_runtime_valid=false;runtime_now=std::numeric_limits<configRUN_TIME_COUNTER_TYPE>::max()-2000;
  for(auto& idle:idle_now)idle=runtime_now;
  sample_cpu_load();runtime_now+=10000;
  for(int core=0;core<configNUMBER_OF_CORES;++core)idle_now[core]+=core ? 7500 : 2500;
  sample_cpu_load();assert(cpu_core_load_valid&&cpu_core_load_pct[0]==75);
  if(configNUMBER_OF_CORES==2)assert(cpu_core_load_pct[1]==25&&cpu_load_pct==50);
  else assert(cpu_load_pct==75);
  runtime_now+=1000;for(auto& idle:idle_now)idle+=995;
  sample_cpu_load();assert(cpu_load_pct==1);
  for(auto value:cpu_core_load_pct)assert(value==1);
#else
  assert(cpu_load_pct==0&&!cpu_runtime_valid);
#endif
  puts("PASS: actual CPU sampler/JSON: per-core loads, unchanged total, warmup, missing stats/idle task, zero interval, rounding, clamping and runtime-counter wrap");
}

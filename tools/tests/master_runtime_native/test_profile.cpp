#include "../../../OpenFumeExtractorMaster/src/MasterRuntimeProfile.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <type_traits>
#define WEB_ENABLE 1
#define configGENERATE_RUN_TIME_STATS 1
#define CONFIG_ARDUINO_RUNNING_CORE 1
#define F(x) x
using configRUN_TIME_COUNTER_TYPE = uint32_t;
struct FakeTask { uint32_t runtime=0; } tasks[4];
using TaskHandle_t = FakeTask*;
struct TaskStatus_t { uint32_t ulRunTimeCounter=0; };
static constexpr int pdFALSE=0,eInvalid=0;
static TaskHandle_t master_loop_task_handle=&tasks[0],web_service_task_handle=&tasks[1];
static TaskHandle_t mqtt_service_task_handle=&tasks[2],heap_diag_task_handle=&tasks[3];
static bool developer_mode_enabled=false;
static uint32_t clock_ms=0,clock_runtime=0,reads=0;
static uint32_t millis() { return clock_ms; }
static uint32_t cpu_runtime_counter_now() { return clock_runtime; }
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
static void portENTER_CRITICAL(int* mux) { assert(!*mux);++*mux; }
static void portEXIT_CRITICAL(int* mux) { assert(*mux==1);--*mux; }
static void vTaskGetInfo(TaskHandle_t handle,TaskStatus_t* out,int,int) {
  ++reads;out->ulRunTimeCounter=handle->runtime;
}
struct Buffer {
  std::string text;
  void operator+=(const char* s) { text+=s; }
  void operator+=(char c) { text+=c; }
  template <typename T> void operator+=(T v) { static_assert(std::is_integral_v<T>);text+=std::to_string(v); }
};
#include "generated_profile.inc.h"

template <typename Counter> static void counter_tests() {
  ofe_runtime::TaskCounter<Counter> task;
  const Counter before=Counter(-1000);
  assert(!task.sample(1,before,before).valid);
  auto value=task.sample(1,Counter(before+250),Counter(before+1000));
  assert(value.valid&&value.percent==25);
  assert(!task.sample(1,Counter(before+250),Counter(before+1000)).valid);
  assert(!task.sample(2,0,5000).valid); // changed task must establish its own baseline
  value=task.sample(2,1001,6000);assert(value.valid&&value.percent==100);
  task.reset();assert(!task.sample(2,1001,6000).valid);
  assert(!task.sample(0,1001,6000).valid);
}
int main() {
  static_assert(sizeof(ofe_runtime::PhaseWindow)+sizeof(master_runtime_tasks)+sizeof(ofe_runtime::Snapshot)<1024);
  counter_tests<uint32_t>();counter_tests<uint64_t>();
  ofe_runtime::PhaseWindow window;
  assert(window.sync(UINT32_MAX-9,true));window.record(ofe_runtime::Scheduler,100);
  window.record(ofe_runtime::Scheduler,200);auto w=window.take(10);
  assert(w.window_ms==20&&w.phases[ofe_runtime::Scheduler].averageUs()==150);
  assert(w.phases[ofe_runtime::Scheduler].max_us==200);
  assert(window.take(20).phases[ofe_runtime::Scheduler].calls==0);
  window.sync(30,false);window.record(ofe_runtime::Scheduler,100);
  assert(!window.take(40).active);
  Buffer hidden;master_runtime_begin(0);master_runtime_sample(0);append_master_runtime_json(hidden);
  assert(reads==0&&hidden.text.empty());
  developer_mode_enabled=true;master_runtime_begin(0);
  master_runtime_window.record(ofe_runtime::Logic,1500);
  clock_ms=1000;clock_runtime=1000000;master_runtime_sample(clock_ms);
  assert(reads==4&&!master_runtime_last.tasks[0].valid); // warmup is not 0% CPU
  for(int i=0;i<4;++i)tasks[i].runtime+=100000*(i+1);
  master_runtime_window.record(ofe_runtime::Scheduler,2000);
  clock_ms=2000;clock_runtime=2000000;master_runtime_sample(clock_ms);
  for(int i=0;i<4;++i)assert(master_runtime_last.tasks[i].valid&&master_runtime_last.tasks[i].percent==10*(i+1));
  assert(master_runtime_last.phases[ofe_runtime::Scheduler].total_us==2000);
  assert(master_runtime_last.phases[ofe_runtime::Logic].calls==0);
  Buffer json;append_master_runtime_json(json);
  assert(json.text.find("\"id\":\"mqtt\",\"core\":1,\"cpu_pct\":30")!=std::string::npos);
  assert(json.text.find("\"avg_us\":2000")!=std::string::npos);
  web_service_task_handle=nullptr;clock_ms+=1000;clock_runtime+=1000000;master_runtime_sample(clock_ms);
  assert(!master_runtime_last.tasks[ofe_runtime::HttpTask].valid); // inline HTTP belongs to loopTask
  developer_mode_enabled=false;master_runtime_begin(clock_ms);
  const auto previous_reads=reads;master_runtime_sample(clock_ms);
  Buffer off;append_master_runtime_json(off);assert(off.text.empty()&&reads==previous_reads&&!master_runtime_last.active);
  developer_mode_enabled=true;master_runtime_begin(clock_ms);
  clock_ms+=1000;clock_runtime+=1000000;master_runtime_sample(clock_ms);
  assert(!master_runtime_last.tasks[0].valid);
  puts("PASS: actual task/phase profiling hooks+JSON: gated sampling, four task loads, unavailable/fallback tasks, re-enable warmup, window isolation, 32/64-bit wrap and bounded storage");
}

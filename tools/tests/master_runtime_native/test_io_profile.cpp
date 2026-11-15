#include "../../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/HttpPerformance.h"
#include "../../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MqttPerformance.h"
#include "../../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/OfePsramTextBuffer.h"
#include <cassert>
#include <string>
#include <vector>
static uint32_t clock_ms=0,clock_us=0,timer_reads=0;
static uint32_t millis(){return clock_ms;}
static uint32_t micros(){++timer_reads;return clock_us;}
#include "../../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MqttTransportProbe.h"
static bool developer_mode_enabled=false,mqtt_enabled=true;
static int mqtt_perf_mux=0;
static void portENTER_CRITICAL(int* mux){assert(!*mux);++*mux;}
static void portEXIT_CRITICAL(int* mux){assert(*mux==1);--*mux;}
static MqttPerfWindow mqtt_perf_window;
static HttpPerfWindow http_perf_window;
#include "generated_io_profile.inc.h"

struct FakeTransport {
  unsigned writes=0;size_t limit=SIZE_MAX;std::vector<uint8_t> bytes;
  virtual ~FakeTransport()=default;
  virtual size_t write(const uint8_t* data,size_t size){++writes;clock_us+=40;const size_t n=size<limit?size:limit;bytes.insert(bytes.end(),data,data+n);return n;}
  virtual size_t write(uint8_t data){return write(&data,1);}
  void setCACert(const char*){} // inherited TLS configuration must remain available
};
int main(){
  static_assert(sizeof(MqttPerfWindow)+sizeof(HttpPerfWindow)<1024);
  const uint8_t data[]={1,2,3,4};
  OfeMqttTransportClient<FakeTransport,mqtt_transport_probe_enabled,mqtt_transport_probe_record> client;
  client.setCACert("certificate");
  mqtt_perf_tick();client.write(data,4);
  assert(timer_reads==0&&client.writes==1&&client.bytes==std::vector<uint8_t>({1,2,3,4}));
  {HttpPerfScope scope(HttpPerfPhase::StatusBuild);clock_us+=50;}
  assert(timer_reads==0);
  OfePsramTextBuffer json;append_http_perf_json(json);assert(json.empty());

  developer_mode_enabled=true;clock_ms=10;mqtt_perf_tick();
  {MqttPerfScope scope(MqttPerfPhase::MasterBuild);clock_us+=100;scope.finish();}
  {MqttPerfScope scope(MqttPerfPhase::ModuleBuild);clock_us+=300;}
  auto mq=mqtt_perf_snapshot();
  assert(mq.phases[0].calls==2&&mq.phases[0].total_us==400);
  assert(mq.phases[3].calls==1&&mq.phases[4].total_us==300);
  clock_us=UINT32_MAX-10;client.write(data,4); // micros wraps during transport write
  client.limit=2;assert(client.write(data,4)==2);
  client.limit=0;assert(client.write(data,4)==0);
  client.limit=SIZE_MAX;assert(client.write(uint8_t(9))==1);
  assert(client.writes==5&&client.bytes==std::vector<uint8_t>({1,2,3,4,1,2,3,4,1,2,9}));
  mq=mqtt_perf_snapshot();
  assert(mq.transport_bytes==7&&mq.transport_short_writes==2);
  assert(mq.phases[5].calls==4&&mq.phases[5].total_us==160);
  assert(mq.payload_bytes==0&&mq.publish_failed==0); // transport is not publish/broker success
  for(auto phase:{HttpPerfPhase::StatusBuild,HttpPerfPhase::HaBuild,HttpPerfPhase::DiagnosticsBuild}){
    HttpPerfScope build(phase);clock_us+=25;build.finish(1000);build.finish(9000);
  }
  {HttpPerfScope send(HttpPerfPhase::HaSend);clock_us+=80;}
  append_http_perf_json(json);
  assert(strstr(json.c_str(),"\"http_perf\"")&&strstr(json.c_str(),"\"payload_bytes\":3000"));
  auto http=http_perf_window.snapshot(clock_ms);
  assert(http.phases[0].calls==1&&http.phases[2].total_us==25&&http.phases[3].total_us==80);
  clock_ms=5010;mqtt_perf_tick();
  assert(mqtt_perf_snapshot().window_ms==5000&&http_perf_window.snapshot(clock_ms).payload_bytes==3000);
  developer_mode_enabled=false;mqtt_perf_tick();json.clear();append_http_perf_json(json);assert(json.empty());
  const auto reads=timer_reads;client.write(data,4);assert(timer_reads==reads);
  developer_mode_enabled=true;clock_ms=6000;mqtt_perf_tick();assert(mqtt_perf_snapshot().transport_bytes==0);
  mqtt_enabled=false;mqtt_perf_tick();{HttpPerfScope scope(HttpPerfPhase::HaBuild);clock_us+=10;}
  assert(http_perf_window.snapshot(clock_ms).phases[2].calls==1); // HTTP works without MQTT
  HttpPerfWindow wrap;wrap.tick(UINT32_MAX-100,true);wrap.record(HttpPerfPhase::StatusBuild,10,20);wrap.record(HttpPerfPhase::Count,900,99);
  wrap.tick(4999,true);assert(wrap.snapshot(4999).window_ms==5100&&wrap.snapshot(4999).payload_bytes==20);
  puts("PASS: actual HTTP/MQTT hooks, disabled zero-timer path, split build phases, inherited transport, exact writes/short writes, wrap, windows and dev/MQTT toggles");
}

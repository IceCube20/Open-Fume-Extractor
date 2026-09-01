#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <FS.h>
#include <LittleFS.h>
#include <Update.h>
#include <time.h>
#include <ctype.h>
#include <esp_netif.h>
#include "src/MasterBuildConfig.h"
#include "src/bus/Rs485PeripheralBus.h"
#include "src/OfeStatusLed.h"
#include "src/ModuleRegistry.h"
#include "src/ExtractorLogic.h"
#include "src/MasterScheduler.h"
#include "src/MasterSettingsStore.h"
#include "src/OfeFirmwareAuth.h"
#include "src/WebLogo.h"

using namespace jbc_rs485;

static_assert(MODULE_FW_CHUNK_SIZE <= (MAX_PAYLOAD - 4),
              "MODULE_FW_CHUNK_SIZE too large for FW_CHUNK payload");
static_assert(MODULE_FW_DISPLAY_CHUNK_SIZE <= (MAX_PAYLOAD - 4),
              "MODULE_FW_DISPLAY_CHUNK_SIZE too large for FW_CHUNK payload");

#include "src/MasterGlobals.inc.h"
#include "src/MasterDisplayWifi.h"

#if WEB_ENABLE
#include "src/MasterPrototypes.inc.h"
#include "src/WebSecurity.inc.h"
#endif

#include "src/MasterDiagnostics.inc.h"

#include "src/WebStatus.inc.h"

#include "src/MasterCommandQueue.inc.h"

#include "src/MasterNetwork.inc.h"

#include "src/MasterCli.inc.h"
#include "src/MasterMqtt.inc.h"

#include "src/WebShell.inc.h"

#include "src/WebDiagnostics.inc.h"

#include "src/WebLogic.inc.h"

#include "src/WebUpdate.inc.h"

#include "src/WebConfig.inc.h"
#include "src/WebDisplayWifi.inc.h"

#include "src/WebControls.inc.h"

#include "src/WebServer.inc.h"

#include "src/MasterMonitor.inc.h"

#include "src/MasterSetup.inc.h"

void setup() {
  master_setup();
}

#include "src/MasterStatusLed.inc.h"

#include "src/MasterLoop.inc.h"

void loop() {
  master_loop_tick();
}

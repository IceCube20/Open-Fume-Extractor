from pathlib import Path
import re

root = Path(__file__).resolve().parents[3]
out = root / 'tools/tests/build-checks/mcu-capacity-native'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterMqtt.inc.h').read_text(encoding='utf-8-sig')
types = source[source.index('static const uint32_t MQTT_HA_MANIFEST_MAGIC'):source.index('static void* mqtt_manifest_alloc')]
functions = []
for name in ('mqtt_manifest_valid', 'mqtt_manifest_migrate'):
    match = re.search(r'^static bool ' + name + r'\([^;{]+\) \{.*?^}', source, re.M | re.S)
    if not match:
        raise RuntimeError(name)
    functions.append(match.group())
(out / 'manifest.h').write_text(types + '\n' + '\n'.join(functions), encoding='utf-8')
source = (root / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebUpdate.inc.h').read_text(encoding='utf-8-sig')
functions = []
for signature in ('static bool master_update_check_real_bytes(', 'static void web_handle_master_update_upload()'):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    functions.append(source[start:end])
(out / 'multipart.h').write_text('\n'.join(functions), encoding='utf-8')

source = (root / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterScheduler.cpp').read_text(encoding='utf-8')
header = (root / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterScheduler.h').read_text(encoding='utf-8')
(out / 'alarm_codes.inc').write_text(re.search(r'enum DisplayAlarmCode : uint8_t \{.*?\n};', source, re.S).group())
(out / 'alarm_types.inc').write_text(re.search(r'  struct DisplayAlarmItem \{.*?\n  };\n  struct DisplayAlarmSnapshot \{.*?\n  };', header, re.S).group())
(out / 'alarm_snapshot.inc').write_text(re.search(r'void MasterScheduler::buildDisplayAlarmSnapshot\(.*?\n}\n', source, re.S).group())
(out / 'home_metrics.inc').write_text(re.search(r'static ofe_home_metrics::State display_home_metrics\(.*?\n}\n', source, re.S).group())

bridges = []
for folder in ('UniversalRs232Module', 'ModbusRtuModule'):
    source = (root / 'Module' / folder / (folder + '.ino')).read_text(encoding='utf-8')
    functions = [re.search(r'^static (?:bool|void) ' + name + r'\(uint32_t now\) \{.*?^}',
                           source, re.M | re.S).group()
                 for name in ('local_device_online_now', 'update_local_fault_state')]
    fault = re.search(r'^static const uint16_t FAULT_LOCAL_UART_INACTIVE = [^;]+;', source, re.M).group()
    bridges.append('namespace ' + folder + ' {\n' + fault +
                   '\nstatic uint32_t last_local_rx_ms = 0;\nstatic uint16_t fault_mask = 0;\nstatic ofe_local_diag::State local_diagnostics;\n' +
                   '\n'.join(functions) + '\n}\n')
(out / 'bridge_links.inc').write_text('\n'.join(bridges), encoding='utf-8')

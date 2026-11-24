"""Exercise actual P4 central code with deterministic NimBLE adapters (not radio emulation)."""
import os
import json
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TEST = ROOT / "tools/tests/display_hybrid_native"
OUT = ROOT / "tools/tests/build-checks/master-p4-native"


def extract_function(text, name):
    start = re.search(r"^(?:static |bool |void )[^\n]*\b" + name + r"\([^\n]*\) \{", text, re.M).start()
    begin = text.index("{", start)
    depth, end = 1, begin + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    src = ROOT / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"
    tunnel = (src / "bus/OfeDisplayTunnel.h").read_text(encoding="utf-8-sig")
    config = tunnel[tunnel.index("namespace ofe_wifi {"):tunnel.index("inline uint64_t nonce()")]+"}\n"
    shared = (src / "OfeDisplayBle.h").read_text(encoding="utf-8-sig")
    protocol = shared[shared.index("namespace ofe_ble {"):shared.index("class OfeDisplayBle {")]
    central = (src / "OfeP4MasterBle.h").read_text()
    cpp = OUT / "test.cpp"
    cpp.write_text('#include "p4_master_ble_stubs.h"\n#include "MasterFirmwareHeader.h"\n'+config+protocol+
                   '\n#define private public\n'+central[central.index("class OfeDisplayBle {"):]+
                   '\n#undef private\n'+(TEST / "test_master_p4.cpp").read_text(), encoding="utf-8")
    vc = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207")
    sdk = Path("C:/Program Files (x86)/Windows Kits/10")
    ver = "10.0.26100.0"
    env = dict(os.environ)
    env["INCLUDE"] = ";".join(str(p) for p in (vc / "include", sdk / f"Include/{ver}/ucrt", sdk / f"Include/{ver}/shared", sdk / f"Include/{ver}/um"))
    env["LIB"] = ";".join(str(p) for p in (vc / "lib/x64", sdk / f"Lib/{ver}/ucrt/x64", sdk / f"Lib/{ver}/um/x64"))
    exe = OUT / "test.exe"
    subprocess.run([str(vc / "bin/Hostx64/x64/cl.exe"), "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/W4", "/O2",
                    "/I"+str(TEST), "/I"+str(src), str(cpp), "/Fo"+str(OUT)+"/", "/Fe"+str(exe)], env=env, check=True)
    subprocess.run([str(exe)], check=True)
    scheduler_source = (src / "MasterScheduler.cpp").read_text()
    scheduler_header = (src / "MasterScheduler.h").read_text()
    logic_source = (src / "WebLogic.inc.h").read_text()
    settings = (src / "MasterSettingsStore.cpp").read_text()
    types = scheduler_header[scheduler_header.index("  enum InputSourceType"):scheduler_header.index("  MasterScheduler(")]
    routing = scheduler_source[scheduler_source.index("void MasterScheduler::applyInputRuleTarget("):]
    routing = routing[:routing.index("  // Stateful external rule targets")] + "}\n"
    fixture = '''#include <initializer_list>
#include "OfeAutomationState.h"
namespace MasterSettingsStore {
struct MainInputSettings {uint8_t type,addr,bit;};
''' + extract_function(settings, "normalizeMainInput") + '''\n}
class MasterScheduler { public:
''' + types + '''
  unsigned calls=0; uint8_t type=0,addr=0,bit=0,output=0; bool persisted=false;
  bool setMainInputSource(uint8_t t,uint8_t a,uint8_t b,bool p) {++calls;type=t;addr=a;bit=b;persisted=p;return true;}
  void setPreferredOutputAddr(uint8_t a,bool) {output=a;}
  void requestDisplayWake(uint8_t) {}
  void queueExtractorAction(uint8_t) {}
  void setLogicSystemOutput(uint8_t,uint8_t,bool) {}
  void setLogicMarker(uint8_t,bool) {}
  void setLogicExternalInput(bool) {}
  void setLogicContinuous(bool) {}
  bool setModuleOutput(uint8_t,bool,uint16_t) {return true;}
  bool setIoOutput(uint8_t,uint16_t,uint16_t) {return true;}
  bool setUniversalEntity(uint8_t,uint8_t,const uint8_t*,uint8_t) {return true;}
  struct Control {uint16_t select_flow=1000;};
  Control controlSettings() {return {};}
  void applyInputRuleTarget(InputActionRule&,bool);
};
MasterScheduler scheduler;
bool logic_rt_last_main_output=false;
''' + routing + '\n' + '\n'.join(extract_function(logic_source, name) for name in
          ("logic_extractor_action_from_signal", "logic_output_is_event_signal", "logic_apply_output_signal"))
    (OUT / "generated_main_input_actions.h").write_text(fixture, encoding="utf-8")
    input_exe = OUT / "main_input_actions.exe"
    subprocess.run([str(vc / "bin/Hostx64/x64/cl.exe"), "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/W4", "/O2",
                    "/I"+str(OUT), "/I"+str(src), str(TEST / "test_main_input_actions.cpp"),
                    "/Fo"+str(OUT)+"/", "/Fe"+str(input_exe)], env=env, check=True)
    subprocess.run([str(input_exe)], check=True)
    network = (src / "MasterNetworkInterface.h").read_text()
    lines = network[network.index('#if defined(OFE_MASTER_P4_WAVESHARE_POE)'):].splitlines()
    depth = 0
    guard = []
    for line in lines:
        guard.append(line)
        if re.match(r'^#if(?:def|ndef)?\b', line):
            depth += 1
        elif line.startswith('#endif'):
            depth -= 1
            if depth == 0:
                break
    guard_cpp = OUT/'chip_profile_guard.cpp'
    guard_cpp.write_text('\n'.join(guard) + '\n')
    for poe in (False, True):
        for minimum, maximum in ((0,199),(100,199),(300,399),(301,301),(301,399)):
            expected = minimum >= 301 and maximum >= 302 if poe else maximum < 300
            command = [str(vc/'bin/Hostx64/x64/cl.exe'), '/nologo', '/EP',
                       f'/DCONFIG_ESP32P4_REV_MIN_FULL={minimum}', f'/DCONFIG_ESP32P4_REV_MAX_FULL={maximum}']
            if poe:
                command.append('/DOFE_MASTER_P4_WAVESHARE_POE=1')
            result = subprocess.run(command + [str(guard_cpp)], env=env, capture_output=True)
            assert (result.returncode == 0) == expected, (poe,minimum,maximum,result.stderr)
    print('PASS: actual SDK compile guards accept matching PRE-V3/V3.2 profiles and reject mismatches')
    (OUT / "generated_master_network.h").write_text(network[network.index("inline bool master_ethernet_ready()"):])
    identity = (src / "MasterIdentity.h").read_text()
    (OUT / "generated_master_identity.h").write_text(identity[identity.index("inline uint64_t master_factory_uid()"):])
    for variant in ("s3", "p4", "p4-poe"):
        defines = ["/DCONFIG_IDF_TARGET_ESP32P4=1", "/DOFE_MASTER_P4_WAVESHARE=1"] if variant != "s3" else []
        if variant == "p4-poe":
            defines.append("/DOFE_MASTER_P4_WAVESHARE_POE=1")
        network_exe = OUT / ("network_" + variant + ".exe")
        subprocess.run([str(vc / "bin/Hostx64/x64/cl.exe"), "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/W4", "/O2",
                        *defines, "/I"+str(OUT), "/I"+str(src), "/I"+str(ROOT / "tools/tests/settings_store_native/stubs"), str(TEST / "test_master_network.cpp"),
                        "/Fo"+str(OUT)+"/", "/Fe"+str(network_exe)], env=env, check=True)
        subprocess.run([str(network_exe)], check=True)
    web_config=(src / "WebConfig.inc.h").read_text(encoding="utf-8-sig")
    form=web_config[web_config.index("static void web_append_ipv4_settings("):web_config.index("static void web_handle_config()")]
    form_cpp=OUT / "ipv4_form.cpp"
    form_cpp.write_text('''#include <fstream>
#include "Arduino.h"
#include "MasterIpv4Settings.h"
#define F(s) s
bool german=true;
const char* web_text(const char* de,const char* en) { return german ? de : en; }
'''+form+'''
int main(int argc,char** argv) {
  if(argc!=2)return 1;
  for(const bool de:{true,false}) {
    german=de;
    MasterIpv4Settings wifi{true,IPAddress(192,0,2,10),IPAddress(192,0,2,1),IPAddress(255,255,255,0),IPAddress(192,0,2,1),IPAddress(0,0,0,0)};
    MasterIpv4Settings ethernet;
    String html;
    web_append_ipv4_settings(html,"","WLAN",wifi);
    web_append_ipv4_settings(html,"eth_","LAN / Ethernet",ethernet);
    std::ofstream file(std::string(argv[1])+(de ? "/ipv4-de.html" : "/ipv4-en.html"));
    file<<html.c_str();
    if(!file.good())return 2;
  }
}
''',encoding="utf-8")
    c6_web=(src / "WebHostedC6.inc.h").read_text(encoding="utf-8-sig")
    c6_form=c6_web[c6_web.index('static void append_hosted_c6_ui(String& html)'):c6_web.rindex('\n#else')]
    fixture=form_cpp.read_text(encoding="utf-8")
    fixture=fixture.replace('int main(int argc,char** argv)', '''#include "HostedC6Firmware.h"
#include "HostedC6WebUi.h"
#include "WebRs485Quality.h"
#include "WebDiagnosticExports.h"
#include "WebTypography.h"
String html_escape(const String& value) {return value;}
#include "WebHardwareInfo.h"
char web_csrf_token[]="0123456789abcdef";
void web_csrf_init() {}
bool web_is_german() {return german;}
bool trialAllowed=false;
bool web_c6_trial_allowed() {return trialAllowed;}
bool approved2131=false;
bool web_c6_approved_2131() {return approved2131;}
'''+c6_form+'''\nint main(int argc,char** argv)''')
    fixture=fixture.replace('if(!file.good())return 2;', '''if(!file.good())return 2;
    String c6;append_hosted_c6_ui(c6);
    std::ofstream c6file(std::string(argv[1])+(de ? "/c6-de.html" : "/c6-en.html"));
    c6file<<c6.c_str();if(!c6file.good())return 3;
    approved2131=true;String approved;append_hosted_c6_ui(approved);approved2131=false;
    std::ofstream approvedfile(std::string(argv[1])+(de ? "/c6-approved-de.html" : "/c6-approved-en.html"));
    approvedfile<<approved.c_str();
    approved2131=true;trialAllowed=true;String approvedTrial;append_hosted_c6_ui(approvedTrial);approved2131=false;trialAllowed=false;
    std::ofstream approvedTrialFile(std::string(argv[1])+(de ? "/c6-approved-trial-de.html" : "/c6-approved-trial-en.html"));
    approvedTrialFile<<approvedTrial.c_str();
    trialAllowed=true;String trial;append_hosted_c6_ui(trial);trialAllowed=false;
    std::ofstream trialfile(std::string(argv[1])+(de ? "/c6-trial-de.html" : "/c6-trial-en.html"));
    trialfile<<trial.c_str();if(!trialfile.good())return 8;
    String quality;web_append_rs485_quality(quality);quality+=OFE_RS485_QUALITY_SCRIPT;
    std::ofstream qualityfile(std::string(argv[1])+(de ? "/quality-de.html" : "/quality-en.html"));
    qualityfile<<quality.c_str();if(!qualityfile.good())return 4;
    String exports;web_append_diagnostic_exports(exports);
    std::ofstream exportfile(std::string(argv[1])+(de ? "/diagnostic-exports-de.html" : "/diagnostic-exports-en.html"));
    exportfile<<exports.c_str();if(!exportfile.good())return 6;
    std::ofstream stylefile(std::string(argv[1])+"/shared-form-style.html");
    // Large CSS literals exceed MSVC limits; Python emits the same source below.
    for(unsigned variant=0;variant<3;++variant) {
      const bool p4=variant!=0,poe=variant==2;
      MasterHardwareInfo info;info.p4=p4;info.model=p4 ? "ESP32-P4" : "ESP32-S3";
      info.board=poe ? "Waveshare ESP32-P4-WIFI6-POE-ETH" : p4 ? "Waveshare Module DEV KIT" : "ESP32-S3 (MCU-S3)";
      info.build_board=poe ? "OFE_MCU_P4_POE" : p4 ? "OFE_MCU_P4" : "ESP32S3_DEV";
      info.revision=p4 ? 100 : 2;info.cores=2;info.cpu_mhz=p4 ? 360 : 240;
      info.flash_bytes=16*1048576;info.flash_hz=80000000;info.flash_mode="DIO";
      info.psram_bytes=(p4 ? 32 : 8)*1048576;info.psram_mhz=p4 ? 200 : 80;
      info.arduino=p4 ? "3.3.12" : "3.2.0";info.idf=p4 ? "v5.5.5" : "v5.4.1-1-g2f7dcd862a-dirty";
      info.c6_current=0x020C0D;info.c6_host=0x020C0D;
      String hardware;web_append_hardware_info(hardware,info);
      std::ofstream hwfile(std::string(argv[1])+"/hardware-"+(poe ? "p4-poe-" : p4 ? "p4-" : "s3-")+(de ? "de.html" : "en.html"));
      hwfile<<hardware.c_str();if(!hwfile.good())return 5;
    }''')
    form_cpp.write_text(fixture,encoding="utf-8")
    form_exe=OUT / "ipv4_form.exe"
    subprocess.run([str(vc / "bin/Hostx64/x64/cl.exe"),"/nologo","/EHsc","/std:c++17","/utf-8","/W4","/O2",
                    "/DARDUINO=10819","/I"+str(src),"/I"+str(ROOT / "tools/tests/settings_store_native/stubs"),str(form_cpp),
                    "/Fo"+str(OUT)+"/","/Fe"+str(form_exe)],env=env,check=True)
    subprocess.run([str(form_exe),str(OUT)],check=True)
    typography=(src / "WebTypography.h").read_text()
    base=typography.split("#define OFE_WEB_TYPOGRAPHY_STYLE ",1)[1].split(" OFE_WEB_FORM_STYLE",1)[0]
    styles=json.loads(base)
    for name in ("WebFormStyles.h","WebHeaderStyles.h","WebTheme.h"):
        text=(src / name).read_text()
        styles+=re.search(r'R"([A-Z]+)\(([\s\S]*?)\)\1"',text)[2]
    (OUT / "shared-form-style.html").write_text(styles,encoding="utf-8")
    activity=(src / "MasterEthernetActivity.h").read_text()
    (OUT / "generated_ethernet_activity.h").write_text(re.sub(r'^#include.*\n','',activity,flags=re.M))
    for variant,defines in (("s3",[]),("p4",["/DCONFIG_IDF_TARGET_ESP32P4=1","/DCONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC=1"]),
                            ("p4-no-events",["/DCONFIG_IDF_TARGET_ESP32P4=1"])):
        activity_exe=OUT / ("ethernet_activity_"+variant+".exe")
        subprocess.run([str(vc / "bin/Hostx64/x64/cl.exe"),"/nologo","/EHsc","/std:c++17","/utf-8","/W4","/O2",
                        *defines,"/I"+str(OUT),str(TEST / "test_master_ethernet_activity.cpp"),
                        "/Fo"+str(OUT)+"/","/Fe"+str(activity_exe)],env=env,check=True)
        subprocess.run([str(activity_exe)],check=True)
    for test in ("test_hosted_c6","test_hosted_c6_staging"):
        c6_exe=OUT / (test+".exe")
        if test.endswith("staging"):
            updater=(src / "HostedC6Updater.h").read_text()
            (OUT / "generated_hosted_c6_updater.h").write_text(re.sub(r'^#include.*\n','',updater,flags=re.M))
        subprocess.run([str(vc / "bin/Hostx64/x64/cl.exe"),"/nologo","/EHsc","/std:c++17","/utf-8","/W4","/O2","/DCONFIG_IDF_TARGET_ESP32P4=1",
                        "/DARDUINO=10819","/I"+str(OUT),"/I"+str(src),"/I"+str(ROOT / "tools/tests/settings_store_native/stubs"),str(TEST / (test+".cpp")),
                        "/Fo"+str(OUT)+"/","/Fe"+str(c6_exe),"bcrypt.lib"],env=env,check=True)
        fixture=ROOT / "tools/tests/build-checks/hosted-c6/esp32c6-v2.12.13.bin"
        args=[str(fixture)] if test.endswith("staging") and fixture.exists() else []
        trial_fixture=fixture.with_name('esp32c6-v2.13.1.bin')
        if args and trial_fixture.exists():args.append(str(trial_fixture))
        subprocess.run([str(c6_exe)]+args,check=True)


if __name__ == "__main__":
    main()

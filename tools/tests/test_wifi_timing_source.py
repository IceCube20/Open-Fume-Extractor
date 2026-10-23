"""Keep diagnostic timing opt-in, authenticated, bounded and presentation-only."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
MASTER = ROOT / 'OpenFumeExtractorMaster/src'
NAMES = ('DisplayModule_320x480', 'DisplayModule_800x480', 'DisplayModule_ST7796_320x480')


class WifiTimingSource(unittest.TestCase):
    def test_shared_wire_and_display_adapters_match(self):
        wire = (MASTER / 'bus/OfeDisplayTunnel.h').read_bytes()
        first = None
        for name in NAMES:
            base = ROOT / 'Module' / name / 'src'
            self.assertEqual(wire, (base / 'OfeDisplayTunnel.h').read_bytes())
            source = (base / 'OfeDisplayWifi.h').read_text(encoding='utf-8')
            adapter = (source[source.index('  bool send(uint8_t kind,'):source.index('  static bool sendHook(')],
                       source[source.index('  bool poll(jbc_rs485::Frame& frame)'):source.index('  static void workerHook(')])
            if first is None:
                first = adapter
            self.assertEqual(first, adapter)
            self.assertIn('bool timing_enabled_=false;', source)
            self.assertLess(source.index('if (!connected_ || !inSession('), source.index('const bool timed=p.kind==TIMED_DATA;'))
            self.assertIn('timing_enabled_ && getTimedFrame(', source)
            self.assertLess(source.index('timing_rx_polls_.sample(received_us)'), source.index('    Packet p;', source.index('  bool poll(jbc_rs485::Frame& frame)')))

    def test_master_negotiation_and_ota_stay_separate(self):
        header = (MASTER / 'MasterDisplayWifi.h').read_text(encoding='utf-8')
        source = (MASTER / 'MasterDisplayWifi.cpp').read_text(encoding='utf-8')
        self.assertIn('timing_enabled_{false}', header)
        self.assertIn('timingEnabled() && p.timing_capable', source)
        self.assertIn('!firmware_addr_.load()', source)
        send = source.split('bool MasterDisplayWifi::send(', 1)[1].split('void MasterDisplayWifi::resetTiming()', 1)[0]
        self.assertEqual(send.count('sendPacket('), 1)
        self.assertIn('frame->dst==p.addr', send)
        receive = source.split('bool MasterDisplayWifi::receive(', 1)[1]
        self.assertLess(receive.index('!inSession(msg,'), receive.index('getTimedFrame(msg,'))
        self.assertIn('resetPeerTiming(*p)', receive)
        self.assertLess(receive.index('peer.timing.polled(received_us)'), receive.index('if (n<'))
        self.assertIn('packet.body[2]!=p.timing_version', send)
        self.assertIn('p.timing_cap_generation==timing_generation_.load()', send)
        scheduler = (MASTER / 'MasterScheduler.cpp').read_text(encoding='utf-8')
        self.assertIn('if (rx_via_network) master_display_wifi.responseProcessed(resp)', scheduler)
        self.assertIn('master_display_wifi.responseProcessed(frame)', scheduler)

    def test_controls_and_payload_are_developer_gated(self):
        source = (MASTER / 'WebDiagnostics.inc.h').read_text(encoding='utf-8')
        control = source.split('static void web_handle_diagnostics_control()', 1)[1].split('static void web_handle_diagnostics()', 1)[0]
        self.assertLess(control.index('if (!developer_mode_enabled)'), control.index('master_display_wifi.resetTiming()'))
        self.assertIn('if (developer_mode_enabled) {\n      if (m.type == MODULE_DISPLAY) diagnostics_append_wifi_timing_json', source)
        self.assertIn('if (developer_mode_enabled) {\n    html += F("<div class=\'diag-wifi-timing-control', source)
        settings = (MASTER / 'MasterMqtt.inc.h').read_text(encoding='utf-8')
        self.assertIn('if (!enabled) master_display_wifi.setTimingEnabled(false)', settings)

    def test_real_javascript_formats_both_languages_and_empty_states(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('Node.js unavailable')
        source = (MASTER / 'WebDiagnostics.inc.h').read_text(encoding='utf-8')
        start = source.index('function diagWifiTimingHtml(')
        end = source.index('\nfunction diagModuleSignature(', start)
        script = '''const assert=require('node:assert/strict');
let UI_DE=true;function u(de,en){return UI_DE?de:en}function fmtAge(ms){return ms+' ms'}
''' + source[start:end] + r'''
const metric={samples:2,last_us:1234,avg_us:1000,max_us:20000};
const peak={rtt_us:90000,display_us:1000,transport_us:89000,master_poll_us:2000,display_poll_us:3000,master_queue_us:null,command:80,sequence:15};
const m={transport:'wifi',wifi_timing:{version:2,supported:true,rtt:metric,display:metric,transport:metric,master_queue:metric,master_poll:metric,display_poll:metric,peak,sent:3,age_ms:20,invalid:1}};
assert.equal(diagWifiTimingHtml(m,true,false),'');assert.equal(diagWifiTimingHtml(m,false,false),'');assert.equal(diagWifiTimingHtml(m,true),'');
let html=diagWifiTimingHtml(m,true,true);assert.match(html,/WLAN-Zeitmessung/);assert.match(html,/1.23 ms/);assert.match(html,/max 20.00 ms/);assert.match(html,/Display-Bearbeitung/);assert.match(html,/2 \/ 3/);
UI_DE=false;html=diagWifiTimingHtml(m,false,true);assert.match(html,/WiFi timing/);assert.match(html,/stopped/);assert.match(html,/Master until processing/);
assert.match(html,/90.00 ms/);assert.match(html,/89.00 ms/);assert.match(html,/2.00 ms/);assert.match(html,/3.00 ms/);assert.match(html,/SEQ 0x0F/);
delete m.wifi_timing.version;assert.match(diagWifiTimingHtml(m,true,true),/firmware/i);m.wifi_timing.version=2;
assert.equal(diagWifiTimingHtml({...m,transport:'rs485'},true,true),'');
assert.equal(diagWifiTimingHtml({transport:'wifi'},true,true),'');
m.wifi_timing.rtt={samples:0};assert.equal(diagWifiTimingHtml(m,false,true),'');
assert.match(diagWifiTimingHtml(m,true,true),/Awaiting samples/);
m.wifi_timing.supported=false;assert.match(diagWifiTimingHtml(m,true,true),/Awaiting display support/);
'''
        subprocess.run([node, '-e', script], check=True, capture_output=True, text=True, timeout=15)


if __name__ == '__main__':
    unittest.main()

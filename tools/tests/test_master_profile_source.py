"""Keep task CPU profiling bounded, developer-only and distinct from wall time."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster/src'


class MasterProfileSource(unittest.TestCase):
    def test_task_sampler_is_gated_and_has_no_task_enumeration(self):
        source = (ROOT / 'MasterDiagnostics.inc.h').read_text(encoding='utf-8')
        sample = source.split('static void master_runtime_sample(', 1)[1].split('template <typename JsonBuffer>', 1)[0]
        self.assertLess(sample.index('if (!developer_mode_enabled) return;'), sample.index('vTaskGetInfo('))
        self.assertIn('pdFALSE, eInvalid', sample)
        self.assertIn('if (!handles[i])', sample)
        self.assertNotIn('uxTaskGetSystemState', source)
        self.assertNotIn('malloc(', sample)
        self.assertNotIn('xTaskCreate', sample)
        self.assertLess(sample.index('vTaskGetInfo('), sample.index('portENTER_CRITICAL('))
        setup = (ROOT / 'MasterSetup.inc.h').read_text(encoding='utf-8')
        self.assertIn('"mqtt", 8192, nullptr, 1, &mqtt_service_task_handle, 1)', setup)

    def test_loop_keeps_order_and_all_phase_records(self):
        source = (ROOT / 'MasterLoop.inc.h').read_text(encoding='utf-8')
        self.assertLess(source.index('master_command_queue_process();'), source.index('module_update_pump();'))
        self.assertLess(source.index('module_update_pump();'), source.index('scheduler.tick();'))
        self.assertLess(source.index('scheduler.tick();'), source.index('logic_runtime_tick();'))
        self.assertLess(source.index('sample_cpu_load();'), source.index('master_runtime_sample(now);'))
        for phase in ('Local', 'Command', 'Ota', 'Scheduler', 'Logic', 'Maintenance'):
            self.assertEqual(source.count('master_runtime_window.record(ofe_runtime::' + phase + ','), 1)
        self.assertIn('delay(1);', source)
        for name in ('WebStatus.inc.h', 'WebDiagnostics.inc.h'):
            source = (ROOT / name).read_text(encoding='utf-8')
            self.assertIn('append_master_runtime_json(json);', source)
            self.assertIn('runtimeProfileHtml(d.runtime_profile,', source)

    def test_real_js_cpu_percent_and_phase_wall_times_are_not_confused(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('Node.js unavailable')
        source = (ROOT / 'WebShell.inc.h').read_text(encoding='utf-8')
        function = 'function runtimeProfileHtml(' + source.split('function runtimeProfileHtml(', 1)[1].split('</script>', 1)[0]
        script = "const assert=require('node:assert/strict');\n" + function + r'''
const s={active:true,window_ms:1000,age_ms:5,tasks:[{id:'loop',core:1,cpu_pct:40},{id:'http',core:1,cpu_pct:0},{id:'mqtt',core:1,cpu_pct:null},{id:'heap',core:-1,cpu_pct:2}],phases:{scheduler:{calls:100,avg_us:2000,max_us:5000,total_us:200000}}};
let html=runtimeProfileHtml(s,true);assert.match(html,/Steuerloop \(Core 1\): <strong>40%/);
assert.match(html,/HTTP \/ WLAN \(Core 1\): <strong>0%/);assert.match(html,/MQTT \(Core 1\): <strong>-/);
assert.match(html,/keine CPU-Prozente/);assert.match(html,/200.0 ms\/s/);assert.match(html,/2.00 ms/);assert.match(html,/5.00 ms/);
html=runtimeProfileHtml(s,false);assert.match(html,/Control loop/);assert.match(html,/not CPU percentages/);assert.match(html,/floating \/ unknown/);
assert.equal(runtimeProfileHtml(null,true),'');
for(const window_ms of [0,-1,NaN,Infinity])assert.equal(runtimeProfileHtml({...s,window_ms},false),'Awaiting sample window');
s.tasks.push({id:'<img src=x>',core:1,cpu_pct:70});assert.doesNotMatch(runtimeProfileHtml(s,true),/<img/);
s.tasks[0].cpu_pct=200;assert.match(runtimeProfileHtml(s,true),/Steuerloop \(Core 1\): <strong>-/);
'''
        subprocess.run([node, '-e', script], check=True, capture_output=True, text=True, timeout=15)


if __name__ == '__main__':
    unittest.main()

"""Live JSON stays fresh; optimize encoding, not observation intervals."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class WebSerializationSource(unittest.TestCase):
    def test_live_state_escapes_directly_without_temporary_strings(self):
        source = (ROOT / 'WebStatus.inc.h').read_text(encoding='utf-8')
        builder = source.split('static bool build_state_json(', 1)[1].split('static String normalized_hostname', 1)[0]
        self.assertNotIn('json += json_escape(', builder)
        self.assertIn('json.appendJsonEscaped(m.label)', builder)
        self.assertIn('ofe_json::appendEscaped(s,', source)
        self.assertIn('if (size && !out.reserve(size)) return out;', source)

    def test_mqtt_reuses_module_state_capacity_without_changing_publish(self):
        source = (ROOT / 'MasterMqtt.inc.h').read_text(encoding='utf-8')
        builder = source.split('static void mqtt_publish_module_state(', 1)[1].split('static bool mqtt_apply_module_command', 1)[0]
        self.assertIn('static String json;', builder)
        self.assertLess(builder.index('json.remove(0);'), builder.index('json.reserve(3400)'))
        self.assertIn('mqtt_publish_measured(mqtt_module_topic(m.addr, "state").c_str(), json.c_str(), true);', builder)

    def test_diagnostics_reuses_psram_only_and_does_not_send_partial_json(self):
        source = (ROOT / 'WebDiagnostics.inc.h').read_text(encoding='utf-8')
        handler = source.split('static void web_handle_diagnostics_state()', 1)[1].split('static void web_handle_diagnostics_events()', 1)[0]
        self.assertIn('external.reservePsram(52000)', handler)
        self.assertIn('OfePsramTextBuffer temporary;', handler)
        self.assertIn('OfePsramTextBuffer& json = *target;', handler)
        self.assertIn('json.clear();', handler)
        self.assertNotIn('String json;', handler)
        self.assertIn('if (!json.ok() || json.empty())', handler)
        self.assertLess(handler.index('if (!json.ok()'), handler.index('web.sendContent(json.c_str(), json.length())'))
        self.assertIn('web.setContentLength(CONTENT_LENGTH_NOT_SET);', handler)
        self.assertIn('setInterval(loadDiag,500);', source)
        status = (ROOT / 'WebStatus.inc.h').read_text(encoding='utf-8')
        self.assertIn('setInterval(load,500);', status)
        state = (ROOT / 'MasterMqtt.inc.h').read_text(encoding='utf-8').split('static void web_handle_state()', 1)[1].split('static void web_handle_descriptors()', 1)[0]
        self.assertIn('build_state_json(payload,', state)

    def test_timing_is_gated_and_inset_matches_module_body(self):
        source = (ROOT / 'WebDiagnostics.inc.h').read_text(encoding='utf-8')
        self.assertIn('developer!==true||', source)
        self.assertIn('d.developer_mode===true)', source)
        self.assertIn('timingControls.hidden=d.developer_mode!==true', source)
        self.assertIn('body:not(.dev-mode) .diag-wifi-timing', source)
        self.assertIn('.diag-wifi-timing-control[hidden]{display:none}', source)
        self.assertIn('margin:8px 14px 14px', source)
        self.assertIn('max-width:calc(100% - 28px)', source)
        self.assertIn('box-sizing:border-box;overflow-wrap:anywhere', source)


if __name__ == '__main__':
    unittest.main()

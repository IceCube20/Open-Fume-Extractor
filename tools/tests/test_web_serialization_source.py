"""Live JSON stays fresh; optimize encoding, not observation intervals."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / 'OpenFumeExtractorMaster/src'


class WebSerializationSource(unittest.TestCase):
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

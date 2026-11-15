from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('mcu_text_tool', ROOT / 'tools/maintenance/rename_mcu_texts.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class McuTextsAndDefaultsTests(unittest.TestCase):
    def test_mechanical_rewrite_preserves_machine_contracts(self):
        source = '''// Master comment
class MasterScheduler {}; const char* a="Master FW";
const char* b="master"; const char* c="master-cfg";
const char* d="OFE_FW_SIG:v1;target=MASTER;";
const char* e="https://example.test/master/";
const char* html=R"HTML(<h2>Master OTA</h2><div class="section-title master-section-title" id="Master" data-label="Master" title="Master system"></div><a href="/Master/">Master</a><script>const master={};let label='Update the master';let value='master';</script>)HTML";'''
        result = tool.cpp_js(source)
        for stable in ('// Master comment', 'class MasterScheduler', '"master"', '"master-cfg"', 'target=MASTER;', 'https://example.test/master/', 'const master={}', "value='master'"):
            self.assertIn(stable, result)
        self.assertIn('"MCU FW"', result)
        self.assertIn('<h2>MCU OTA</h2>', result)
        self.assertIn("'Update the MCU'", result)
        self.assertIn('class="section-title master-section-title"', result)
        self.assertIn('id="Master" data-label="Master" title="MCU system"', result)
        self.assertIn('href="/Master/">MCU</a>', result)

    def test_no_main_input_is_the_new_default(self):
        header = (ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterScheduler.h').read_text()
        store = (ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterSettingsStore.cpp').read_text()
        self.assertIn('main_input_source_type_ = INPUT_SRC_NONE', header)
        self.assertIn('getUChar(KEY_LEGACY_JBC_INPUT, 0)', store)
        for path in (ROOT / 'Module').glob('DisplayModule_*/*.ino'):
            self.assertIn('uint8_t main_input_source_type = 0;', path.read_text(encoding='utf-8-sig'))

    def test_nested_javascript_urls_survive_visible_text_rename(self):
        source = '''html += F("run.finalizing=true;fetch('/update/master/end',{method:'POST'});stat.textContent='Restart the master';");
html += F("try{await fetch('/update/master/abort?owner=master',{method:'POST'});}catch(e){stat.textContent='Master update failed';}");'''
        result = tool.cpp_js(source)
        self.assertIn("'/update/master/end'", result)
        self.assertIn("'/update/master/abort?owner=master'", result)
        self.assertIn("'Restart the MCU'", result)
        self.assertIn("'MCU update failed'", result)

    def test_fresh_info_is_authoritative_and_not_written_back_automatically(self):
        scheduler = (ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterScheduler.cpp').read_text()
        web = (ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebStatus.inc.h').read_text()
        body = web.split('static void apply_module_labels()', 1)[1].split('\n}\n', 1)[0]
        self.assertIn('if (m.info_name_valid)', body)
        self.assertNotIn('scheduler.setModuleLabel', body)
        self.assertIn('rec->info_name_valid = name_len > 0;', scheduler)
        self.assertIn('ofe_module_label::from_info', scheduler)

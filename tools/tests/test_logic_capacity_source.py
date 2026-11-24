from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class LogicCapacityTests(unittest.TestCase):
    def test_all_mcus_share_64_nodes_and_128_connections(self):
        for board in ('OpenFumeExtractorMCU_S3', 'OpenFumeExtractorMCU_P4', 'OpenFumeExtractorMCU_P4_POE'):
            source = (ROOT / 'OpenFumeExtractorMaster' / board / 'src/WebLogic.inc.h').read_text(encoding='utf-8')
            for name, expected in (('LOGIC_RT_MAX_NODES', 64), ('LOGIC_RT_MAX_LINKS', 128), ('LOGIC_DEF_MAX', 32), ('LOGIC_RT_MAX_OUTPUTS', 32)):
                self.assertEqual(int(re.search(name+r' = (\d+);', source)[1]), expected)

    def test_api_supplies_limits_and_explains_rejections_before_storage(self):
        source = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
        self.assertIn('json += ",\\\"max_nodes\\\":"; json += LOGIC_RT_MAX_NODES;', source)
        self.assertIn('json += ",\\\"max_links\\\":"; json += LOGIC_RT_MAX_LINKS;', source)
        save = source.split('static bool save_logic_slot_json(', 1)[1].split('\n}', 1)[0]
        self.assertLess(save.index('logic_compile_json(stored, slot, compiled, error)'), save.index('logic_save_fs'))
        self.assertIn('error == LogicSaveError::NodesLimit) web.send(400', source)
        self.assertIn('error == LogicSaveError::LinksLimit) web.send(400', source)
        self.assertIn('error == LogicSaveError::OutputsLimit) web.send(400', source)
        self.assertIn('LogicSaveError::Memory) web.send(503', source)

    def test_allocations_scale_with_graph_not_capacity(self):
        source = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
        self.assertIn('logic_rt_alloc(sizeof(LogicRtNode) * node_capacity)', source)
        self.assertIn('logic_rt_alloc(sizeof(LogicRtLink) * link_capacity)', source)
        self.assertNotIn('logic_rt_alloc(sizeof(LogicRtNode) * LOGIC_RT_MAX_NODES)', source)
        self.assertNotIn('logic_rt_alloc(sizeof(LogicRtLink) * LOGIC_RT_MAX_LINKS)', source)

    def test_creation_duplication_links_and_saving_have_guards(self):
        source = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
        for function in ('function addNode(', 'function duplicateSelected(', 'function addLink(', 'async function saveGraph('):
            line = next(line for line in source.splitlines() if line.startswith(function))
            self.assertIn('ensureLogicCapacity(', line)
        self.assertIn('applyLogicCapacity(d)', source)
        self.assertIn('new TextEncoder().encode(body).length>24000', source)
        assets = (SRC / 'WebLogicResponsive.h').read_text(encoding='utf-8')
        self.assertIn("count+' / '+max", assets)
        self.assertIn('function logicCapacityMessage(', assets)
        self.assertIn('distinct output targets per definition.', assets)
        self.assertIn('warning.hidden=false;msg(text);alert(text);return false', assets)


if __name__ == '__main__':
    unittest.main()

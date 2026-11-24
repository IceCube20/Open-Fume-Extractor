from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class LogicSelectionTests(unittest.TestCase):
    def test_selection_is_editor_state_not_graph_data(self):
        source = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        self.assertIn('let logicSelectedNodes=new Set()', source)
        self.assertIn('logicSelectionGraph!==graph', source)
        self.assertNotIn('graph.selected', source)
        page = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
        self.assertIn('syncLogicSelection()', page)
        self.assertIn('logicSelectedNodes.size>1?null:', page)

    def test_group_motion_uses_one_bounded_delta_and_translates_internal_wires(self):
        source = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        self.assertIn('item.node.x=item.x+dx;item.node.y=item.y+dy', source)
        self.assertIn('dx=Math.max(left,Math.min(right,dx))', source)
        self.assertIn('logicSelectedNodes.has(l.from)&&logicSelectedNodes.has(l.to)', source)
        self.assertIn('route.p.map(([x,y])=>[x+dx,y+dy])', source)
        self.assertIn('for(const item of g.routes)item.link.route=structuredClone(item.route)', source)

    def test_mouse_touch_and_cancel_are_supported(self):
        source = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        for token in ('ev.ctrlKey||ev.metaKey||ev.shiftKey', "ev.pointerType==='touch'&&!logicSelectionMode", "'lostpointercapture'", "ev.key==='Escape'", "'blur',cancelLogicSelection"):
            self.assertIn(token, source)
        ui = (SRC / 'WebLogicResponsive.h').read_text(encoding='utf-8')
        self.assertIn('.logic-workspace.is-selecting{touch-action:none;', ui)
        self.assertIn('cancelLogicSelection();if(!logicWireGesture', ui)

    def test_group_duplicate_and_delete_keep_graph_consistent(self):
        source = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        self.assertIn('ensureLogicCapacity(graph.nodes.length+nodes.length,graph.links.length+links.length)', source)
        self.assertIn('copy.from=map.get(l.from);copy.to=map.get(l.to)', source)
        self.assertIn('graph.links=graph.links.filter(l=>!ids.has(l.from)&&!ids.has(l.to)&&!logicSelectedLinks.has(logicRouteKey(l)))', source)
        self.assertIn("ev.target.closest('input,select,textarea,[contenteditable=true]')", source)


if __name__ == '__main__':
    unittest.main()

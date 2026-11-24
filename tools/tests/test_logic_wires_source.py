from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class LogicWireTests(unittest.TestCase):
    def test_connection_asset_is_authenticated_and_flash_served(self):
        header = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        page = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
        server = (SRC / 'WebServer.inc.h').read_text(encoding='utf-8')
        self.assertIn('OFE_WEB_LOGIC_CONNECTION_SCRIPT[] PROGMEM', header)
        self.assertIn('src="/logic/connections.js"', page)
        route = next(line for line in server.splitlines() if 'web.on("/logic/connections.js"' in line)
        self.assertIn('if (!web_require_auth()) return;', route)
        self.assertIn('web.send_P(', route)

    def test_snap_is_screen_space_compatible_and_cancellable(self):
        header = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        self.assertIn('touch?32:22', header)
        self.assertIn('canvas.left+point.x*logicZoom', header)
        self.assertIn('node.id===linkFrom.id||out===linkFrom.out', header)
        self.assertIn("ev.key==='Escape'", header)
        self.assertIn("'lostpointercapture'", header)
        self.assertIn('setPointerCapture(ev.pointerId)', header)

    def test_reconnection_commits_after_capacity_guard(self):
        page = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
        line = next(line for line in page.splitlines() if line.startswith('function finishLink('))
        self.assertIn('const previous=graph.links', line)
        self.assertIn('if(!added)graph.links=previous', line)
        self.assertIn('resetLogicWire();render()', line)
        header = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        move = header.split('function moveLogicWire(', 1)[1].split('function endLogicWire', 1)[0]
        self.assertNotIn('graph.links=graph.links.filter', move)

    def test_actual_obstacles_cached_routes_and_moderate_crossing_cost(self):
        header = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        self.assertIn('el.offsetHeight', header)
        self.assertIn('key===logicRouteCache.key', header)
        page = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
        self.assertIn('bends*28+len', page)
        self.assertIn('penalty+=96', page)
        self.assertNotIn('penalty+=72000', page)
        self.assertNotIn('26000+ov*85', page)
        self.assertIn('logic-line-halo', page)
        self.assertIn('logic-line-junction', page)

    def test_dragging_retains_route_anchors_and_fixed_port_order(self):
        header = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        self.assertIn('function stretchLogicRoute(', header)
        self.assertIn('previous.anchor||previous', header)
        self.assertIn('a.port-b.port', header)
        self.assertNotIn('a.distance-b.distance', header)
        self.assertIn('logicRouteKey(link)', header)
        self.assertIn('logicRouteOverlap(candidate', header)
        page = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
        for prefix in ('function autoLayout(', 'function resetGraph(', 'async function loadGraph('):
            line = next(line for line in page.splitlines() if line.startswith(prefix))
            self.assertIn('clearLogicRouteHistory()', line)

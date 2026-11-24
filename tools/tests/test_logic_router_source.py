from pathlib import Path
import gzip
import hashlib
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'


class LogicRouterTests(unittest.TestCase):
    def test_pinned_assets_are_exact_and_reproducibly_compressed(self):
        header = (SRC / 'WebLogicRouterAssets.h').read_text()
        for file, symbol, blob in [('index.js', 'JS', 'f73a026bbb83de097619790cd91b081ca4f5575d'),
                                   ('libavoid.wasm', 'WASM', '5043d8771c08f174d6366374ad472c27df2755ae'),
                                   ('LICENSE', 'LICENSE', 'b885a535c6e8726d4ccc275bd1c36bfaad726a1e')]:
            data = (ROOT / 'assets/vendor/libavoid' / file).read_bytes()
            self.assertEqual(hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest(), blob)
            array = header.split('OFE_LOGIC_ROUTER_' + symbol + '[] PROGMEM = {')[1].split('};')[0]
            compressed = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', array))
            self.assertEqual(gzip.decompress(compressed), data)
            self.assertEqual(compressed, gzip.compress(data, compresslevel=9, mtime=0))

    def test_binary_assets_are_authenticated_and_sent_with_exact_lengths(self):
        server = (SRC / 'WebServer.inc.h').read_text(encoding='utf-8')
        for route, symbol in [('/logic/router.mjs', 'JS'), ('/logic/libavoid.wasm', 'WASM'), ('/logic/router-license.txt', 'LICENSE')]:
            line = next(line for line in server.splitlines() if 'web.on("' + route + '"' in line)
            self.assertIn('if (!web_require_auth()) return;', line)
            self.assertIn('"Content-Encoding", "gzip"', line)
            self.assertIn('sizeof(OFE_LOGIC_ROUTER_' + symbol + ')', line)
        self.assertIn('PSTR("application/wasm")', server)

    def test_manual_layout_is_preserved_and_validated(self):
        page = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8')
        router = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        self.assertIn('(graph.links||[]).map(normaliseLogicLink)', page)
        self.assertIn('r.p.length<=16', router)
        self.assertIn('p.every(Number.isFinite)', router)
        self.assertIn('g.preview=simplifyPts(p)', router)
        self.assertIn('if(!route.blocked&&clean.route', router)
        self.assertIn('logicSegmentGesture=null', router)

    def test_shared_router_and_accessible_manual_tools(self):
        router = (SRC / 'WebLogicConnections.h').read_text(encoding='utf-8')
        self.assertIn('new A.Router(A.OrthogonalRouting)', router)
        self.assertIn('state.router.processTransaction()', router)
        self.assertIn('setupLogicRoutingTools();setupLogicSelection();setupLogicRouter()', router)
        self.assertIn('state.router.deleteConnector(c)', router)
        self.assertIn('logicAvoid.destroy(state.router)', router)
        self.assertIn('directions.size>=3', router)
        ui = (SRC / 'WebLogicResponsive.h').read_text(encoding='utf-8')
        self.assertIn("edit.dataset.logicEditLink", ui)
        self.assertIn('pointer-events:all;touch-action:none', ui)
        for name in ('OpenFumeExtractorMCU_P4', 'OpenFumeExtractorMCU_P4_POE'):
            for file in ('WebLogicConnections.h', 'WebLogicRouterAssets.h', 'WebLogicResponsive.h'):
                self.assertEqual((SRC / file).read_bytes(), (ROOT / 'OpenFumeExtractorMaster' / name / 'src' / file).read_bytes())


if __name__ == '__main__':
    unittest.main()

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
MCU = ROOT / 'OpenFumeExtractorMaster'
S3 = MCU / 'OpenFumeExtractorMCU_S3/src'


class WebLayoutTests(unittest.TestCase):
    def test_all_mcus_share_responsive_logic(self):
        for name in ('OpenFumeExtractorMCU_P4', 'OpenFumeExtractorMCU_P4_POE'):
            source = MCU / name / 'src'
            for file in ('WebLogicResponsive.h', 'WebLogicConnections.h', 'WebLogic.inc.h', 'WebShell.inc.h', 'WebStatus.inc.h', 'WebServer.inc.h', 'WebHeaderStyles.h'):
                self.assertEqual((source / file).read_bytes(), (S3 / file).read_bytes(), (name, file))

    def test_pages_are_bounded_except_logic_designer(self):
        for file in ('WebShell.inc.h', 'WebStatus.inc.h'):
            source = (S3 / file).read_text(encoding='utf-8')
            self.assertIn('main{width:100%;min-width:0;', source)
            self.assertNotIn('width:min(940px,100%)', source)
            self.assertNotIn('width:min(1100px,100%)', source)
            self.assertNotIn('width:min(1240px,100%)', source)
        shared = (S3 / 'WebHeaderStyles.h').read_text(encoding='utf-8')
        logic = (S3 / 'WebLogicResponsive.h').read_text(encoding='utf-8')
        self.assertIn('body>main{max-width:1600px}', shared)
        self.assertIn('main{width:100%!important;max-width:none!important}', logic)

    def test_status_sections_share_full_width(self):
        source = (S3 / 'WebStatus.inc.h').read_text(encoding='utf-8')
        shared = (S3 / 'WebHeaderStyles.h').read_text(encoding='utf-8')
        self.assertIn('body>main:has(>.status-modules){max-width:none}', shared)
        self.assertNotIn('>:not(.status-modules)', shared)
        self.assertIn('<section class="row status-modules"><div id="details"', source)
        self.assertIn('<section class="row table-wrap status-modules"><table>', source)

    def test_zoom_preserves_graph_coordinates(self):
        source = (S3 / 'WebLogic.inc.h').read_text(encoding='utf-8')
        self.assertIn('(ev.clientX-r.left)/logicZoom', source)
        self.assertIn('(ev.clientY-r.top)/logicZoom', source)
        self.assertIn('beginLogicNodeDrag(ev,n)', source)
        selection = (S3 / 'WebLogicConnections.h').read_text(encoding='utf-8')
        self.assertIn('dx:point.x-n.x,dy:point.y-n.y', selection)
        self.assertIn('item.node.x=item.x+dx;item.node.y=item.y+dy', selection)
        self.assertIn('src="/logic/responsive.js"', source)
        self.assertIn('setupLogicResponsive();setupLogicLangSync()', source)

    def test_status_five_cards_only_on_wide_screens(self):
        source = (S3 / 'WebStatus.inc.h').read_text(encoding='utf-8')
        self.assertIn('@media(min-width:1600px){#details{grid-template-columns:repeat(5,minmax(0,1fr))}}', source)
        self.assertIn('@media(max-width:1050px){#details{grid-template-columns:repeat(2,minmax(0,1fr))}}', source)
        self.assertIn('@media(max-width:620px){#details{grid-template-columns:1fr}}', source)

    def test_mcu_system_uses_actual_grid_and_single_separators(self):
        source = (S3 / 'WebStatus.inc.h').read_text(encoding='utf-8')
        self.assertNotIn('.MCU-stats', source)
        self.assertIn("querySelectorAll('.master-stats,#details .module-system')", source)
        self.assertIn('--master-columns', source)
        self.assertIn("adjacent?'block':'none'", source)
        self.assertIn('border-right:0;border-top:0;border-bottom:0;overflow-wrap:anywhere', source)

    def test_mobile_connections_preserve_multiple_sources(self):
        source = (S3 / 'WebLogicResponsive.h').read_text(encoding='utf-8')
        self.assertIn('addLink(select.value,n.id,index)', source)
        self.assertIn('graph.links=graph.links.filter(l=>l!==link)', source)
        self.assertIn("['plan','blocks','properties']", source)
        self.assertIn('scroll-padding-top:calc(var(--logic-header-height,0px) + 72px)', source)
        self.assertIn('minmax(0,1fr)', source)
        self.assertIn('aria-controls', source)
        self.assertIn('ArrowRight', source)

    def test_assets_are_authenticated_and_do_not_expand_html_heap(self):
        assets = (S3 / 'WebLogicResponsive.h').read_text(encoding='utf-8')
        server = (S3 / 'WebServer.inc.h').read_text(encoding='utf-8')
        self.assertIn('OFE_WEB_LOGIC_RESPONSIVE_SCRIPT[] PROGMEM', assets)
        self.assertIn('OFE_WEB_LOGIC_RESPONSIVE_STYLE[] PROGMEM', assets)
        for route in ('/logic/responsive.js', '/logic/responsive.css', '/logic/connections.js'):
            line = next(line for line in server.splitlines() if 'web.on("'+route+'"' in line)
            self.assertIn('if (!web_require_auth()) return;', line)
            self.assertIn('web.send_P(', line)
            self.assertIn('"Cache-Control", "no-store"', line)


if __name__ == '__main__':
    unittest.main()

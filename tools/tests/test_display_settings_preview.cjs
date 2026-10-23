// Render production coordinates captured by the C++ layout test, not a second layout.
const fs = require("node:fs");
const path = require("node:path");
const assert = require("node:assert/strict");
const {chromium} = require("playwright");
const output = path.join(__dirname, "build-checks/display-settings");
const escape = s => s.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
(async () => {
  const browser = await chromium.launch({headless:true, executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try {
    for (const variant of ["standard", "st7796"]) for (const width of [456,776]) for (const lang of [0,1]) {
      const nodes = JSON.parse(fs.readFileSync(path.join(output, variant+"-"+width+"-"+lang+".json"),"utf8").replace(/^\uFEFF/,""));
      const height = width === 776 ? 480 : 320;
      const page = await browser.newPage({viewport:{width:width+24,height}});
      const headings = ["Display","Anzeige","Sleep & backlight","Ruhe & Beleuchtung"];
      const fields = nodes.map(n => {
        const kind = n.kind === "select" ? "select" : n.kind === "button" ? "button" : "label";
        return '<div class="'+kind+(headings.includes(n.text) ? " heading" : "")+'" style="left:'+n.x+'px;top:'+n.y+'px;width:'+n.width+'px;height:'+n.height+'px"><span>'+escape(n.text)+'</span>'+(kind==="select" ? '<b>&#9662;</b>' : "")+'</div>';
      }).join("");
      const bottom = Math.max(...nodes.map(n=>n.y+n.height));
      await page.setContent('<style>*{box-sizing:border-box}body{margin:0;background:#090d12;color:#eef3f8;font:14px Arial,sans-serif}header{height:42px;padding:10px 12px;border-bottom:1px solid #2c3b4a;font-weight:bold}.scroll{position:absolute;left:12px;top:50px;width:'+width+'px;height:'+(height-106)+'px;overflow:auto;scrollbar-width:thin}.content{height:'+(bottom+8)+'px;position:relative}.label,.button,.select{position:absolute;display:flex;align-items:center}.label{color:#8f9ba8;white-space:nowrap}.heading{color:#58b8ff}.button,.select{border:1px solid #3a5068;border-radius:6px;background:#1a2633;color:white;padding:0 12px;white-space:nowrap}.button{justify-content:center}.select span{flex:1}.select b{margin-left:12px;font-size:12px}.label span{max-width:100%}nav{position:absolute;bottom:8px;left:8px;right:8px;display:flex;gap:8px}nav span{flex:1;text-align:center;border:1px solid #3b5570;border-radius:7px;padding:11px 0;background:#172333}nav span:last-child{background:#246bff}</style><header>OFE <span style="float:right">'+(lang ? "Einstellungen" : "Display settings")+'</span></header><div class="scroll"><div class="content">'+fields+'</div></div><nav><span>'+(lang?"Start":"Home")+'</span><span>'+(lang?"Module":"Modules")+'</span><span>'+(lang?"Alarme":"Alarms")+'</span><span>System</span></nav>');
      const clipped = await page.locator(".select span,.button span,.label span").evaluateAll(items => items.filter(e=>e.scrollWidth>e.clientWidth+1).map(e=>e.textContent));
      assert.deepEqual(clipped, [], "Clipped text: "+clipped.join(", "));
      const filename=variant+"-"+width+"-"+lang;
      await page.screenshot({path:path.join(output, filename+".png")});
      if (width===456) {
        await page.locator(".scroll").evaluate(e=>{e.scrollTop=208});
        await page.screenshot({path:path.join(output,filename+"-sleep.png")});
      } else {
        assert(bottom <= height-106, "Large display settings should fit without scrolling");
      }
      await page.close();
    }
  } finally {await browser.close();}
  console.log("PASS: production settings geometry previews in both languages/sizes; no clipped text");
})().catch(error=>{console.error(error);process.exitCode=1});

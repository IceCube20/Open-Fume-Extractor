const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const assert = require("node:assert/strict");
const {chromium} = require("playwright");
const source = fs.readFileSync(path.join(__dirname, "../../OpenFumeExtractorMaster/src/WebStatus.inc.h"), "utf8");
const template = source.match(/^if\(m.type==6\)\{det\+=\x60(.*)\x60\}\}\);document/m)[1];
const render = language => vm.runInNewContext("\x60" + template + "\x60", {
  uiLang: language, m: {addr: 64, type: 6, type_name: "Display 800x480"},
  mn: () => "Display 800x480", moduleMonitorGlyph: () => "", hx: () => "0x40",
  moduleMeta: () => "", t: key => key,
});
const sync = source.match(/^if\(m.type==6\)\{pillText[^\n]+/m)[0];
const command = source.match(/^async function displayBacklightSet[^\n]+/m)[0];
(async () => {
  const browser = await chromium.launch({headless: true, executablePath: process.env.OFE_BROWSER_EXECUTABLE});
  try {
    const css = source.match(/<style>([\s\S]*?)<\/style>/)[1];
    const output = path.join(__dirname, "build-checks/backlight-ui");
    fs.mkdirSync(output, {recursive: true});
    for (const language of ["de", "en"]) for (const width of [390, 1440]) {
      const page = await browser.newPage({viewport: {width, height: 1000}});
      await page.setContent("<style>" + css + ":root{--ofe-font-ui:Arial,sans-serif}</style>" +
        "<body><main><div id='details' style='max-width:430px;margin:auto'>" + render(language) + "</div></main></body>");
      await page.addScriptTag({content: "let uiLang='" + language + "',displayHoldUntil={};" +
        "function t(k){return k} function pillText(){} function set(id,v){const n=document.getElementById(id);if(n)n.textContent=v}" +
        "function val(n,v){if(n)n.value=v} function clearFieldDirty(){} const requests=[];" +
        "async function fetch(url,options){requests.push({url,body:String(options.body)});return {ok:true}}" +
        "function alert(e){throw Error(e)}" + command +
        "function sync(m){" + sync + "}"});
      await page.evaluate(() => sync({type:6,addr:64,online:true,telemetry_valid:true,display_backlight_supported:true,
        display_backlight_mode:1,display_backlight_timeout_min:17,display_brightness_pct:85}));
      assert.equal(await page.inputValue("#dlight_mode_64"), "1");
      assert.equal(await page.inputValue("#dlight_delay_64"), "17");
      await page.selectOption("#dlight_mode_64", "2");
      const sent = await page.evaluate(() => requests);
      assert(sent.at(-1).body.includes("backlight_mode=2"));
      assert(sent.at(-1).body.includes("backlight_timeout=17"));
      assert(!sent.at(-1).body.includes("brightness"), "Do not rewrite unrelated settings");
      await page.selectOption("#dlight_delay_64", []);
      await page.evaluate(() => displayBacklightSet(64));
      assert.equal((await page.evaluate(() => requests)).length, sent.length);
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), "Page overflow");
      for (const selector of ["#dlight_mode_64", "#dlight_delay_64"]) {
        assert(await page.$eval(selector, e => {
          const a=e.getBoundingClientRect(), b=e.closest(".display-card").getBoundingClientRect();
          return a.width > 80 && a.left >= b.left && a.right <= b.right + 1;
        }), selector + " exceeds the module card");
      }
      await page.selectOption("#dlight_delay_64", "17");
      await page.locator("#dlight_delay_64").blur();
      const aligned = await page.evaluate(() => ["dlight_mode_64", "dlight_delay_64"].map(id => document.getElementById(id).getBoundingClientRect().bottom));
      assert(Math.abs(aligned[0] - aligned[1]) < 1, "Dropdowns must share a baseline");
      assert.equal(await page.locator("#dlight_delay_64 option").count(), 60);
      assert.equal(await page.locator("#dlight_delay_64 option").nth(16).textContent(), "17 min");
      assert.equal(await page.locator("#dlight_mode_64 option").nth(1).textContent(), language === "de" ? "Nur im Energiesparmodus" : "Only in power saving");
      await page.screenshot({path: path.join(output, language + "-" + width + ".png"), fullPage:true});
      await page.evaluate(() => { displayHoldUntil={}; sync({type:6,addr:64,online:true,telemetry_valid:true,display_backlight_supported:false}); });
      assert(await page.isHidden("#dlight_mode_64"));
      assert(await page.isHidden("#dlight_delay_64"));
      await page.close();
    }
  } finally { await browser.close(); }
  console.log("PASS: production display card in DE/EN, desktop/mobile, readback, commands, validation, legacy firmware");
})().catch(error => { console.error(error); process.exitCode=1; });

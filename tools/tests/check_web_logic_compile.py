"""Compile the actual logic page/raw-string macros with S3 and P4 GCC."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'
OUT = ROOT / 'tools/tests/build-checks/web-layout'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = (SRC / 'WebLogic.inc.h').read_text(encoding='utf-8-sig')
    page = source[source.index('static void web_handle_logic() {'):]
    fixture = r'''
#include <string>
#define PROGMEM
#define F(value) value
#define FPSTR(value) value
#include "WebAutomationSignals.h"
#include "WebTypography.h"
static const char theme_markup[] = OFE_WEB_TYPOGRAPHY_STYLE;
#include "WebLogicResponsive.h"
#include "WebLogicConnections.h"
#include "WebLogicRouterAssets.h"
static_assert(sizeof(OFE_LOGIC_ROUTER_WASM) < 200000, "Router flash budget");
using String = std::string;
const char* web_text(const char* de, const char*) { return de; }
void web_shell_begin(String&, const char*, const char*, const char*) {}
void web_shell_end(String&) {}
struct { std::string body; void send(int, const char*, const String& s) {body=s;} } web;
''' + page + r'''
int main() {
  web_handle_logic();
  if (std::string(theme_markup).find("data-ofe-theme=light") == std::string::npos) return 3;
  for (const char* token : {"/logic/responsive.css", "/logic/responsive.js", "function loadGraph"}) {
    if (web.body.find(token) == std::string::npos) return 1;
  }
  if (std::string(OFE_WEB_LOGIC_RESPONSIVE_SCRIPT).find("function setupLogicResponsive()") == std::string::npos) return 2;
  return 0;
}
'''
    cpp = OUT / 'logic_page.cpp'
    cpp.write_text(fixture, encoding='utf-8')
    tools = Path(os.environ['LOCALAPPDATA']) / 'Arduino15/packages/esp32/tools'
    compilers = {'S3': tools / 'esp-x32/2601/bin/xtensa-esp32s3-elf-g++.exe',
                 'P4': tools / 'esp-rv32/2601/bin/riscv32-esp-elf-g++.exe'}
    for chip, compiler in compilers.items():
        subprocess.run([str(compiler), '-std=c++17', '-Wall', '-Wextra', '-I'+str(SRC), '-c',
                        str(cpp), '-o', str(OUT / ('logic_page_'+chip+'.o'))], check=True)
        print('PASS: actual logic page/raw-string macros compiled with '+chip+' GCC (not a full firmware build)')


if __name__ == '__main__':
    main()

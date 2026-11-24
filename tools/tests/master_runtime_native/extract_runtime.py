"""Build native fixtures from the actual master functions, not copies of them."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3] / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"
logic = (root / "WebLogic.inc.h").read_text(encoding="utf-8")
limits = [line for line in logic.splitlines() if re.match(r'static constexpr .* LOGIC_RT_(MAX_NODES|MAX_LINKS|MAX_OUTPUTS|INTERVAL_MS) =', line)]
Path(sys.argv[1]).with_name('generated_logic_limits.inc.h').write_text('\n'.join(limits), encoding='utf-8')


def function(source, name):
    match = re.search(r"^(?:static |(?:bool|void) MasterScheduler::)[^\n]*\b" + name + r"\([^\n]*\)(?: const)? \{", source, re.M)
    if not match:
        raise RuntimeError(name)
    pos = source.index("{", match.start())
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    template = re.search(r"^template[^\n]*\n\Z", source[:match.start()], re.M)
    return source[template.start() if template else match.start():end]


types = logic[logic.index("enum LogicRtNodeType"):logic.index("static LogicCompiledDefinition logic_rt_defs")]
inputs = logic[logic.index("struct LogicRtInputs"):logic.index("static void logic_collect_output")]
evaluation = function(logic, "logic_eval_definition")
reference = evaluation.replace("logic_eval_definition", "reference_eval_definition")
reference = re.sub(r"  bool sampled_inputs\[.*?  // Keep the existing pass order, including feedback and stateful blocks.\n", "", reference, flags=re.S)
reference = reference.replace("const LogicRtInputs inputs = logic_node_inputs(def, i);", "const LogicRtInputs inputs = reference_node_inputs(def, i);")
reference = reference.replace("v = sampled_inputs[i];", "v = logic_signal_active(n.signal);")
reference = reference.replace("logic_advance_clock(st, dt_ms, on_ms, off_ms);", "reference_advance_clock(st, dt_ms, on_ms, off_ms);")
tick = function(logic, "logic_runtime_tick")
tick = tick[:tick.index("  const uint32_t mask")] + "  logic_rt_unlock();\n}"
diagnostics = (root / "WebDiagnostics.inc.h").read_text(encoding="utf-8")
scheduler = (root / "MasterScheduler.cpp").read_text(encoding="utf-8")
pieces = [types, function(logic, "logic_index_links"), inputs,
          function(logic, "logic_collect_output"), function(logic, "logic_advance_clock"),
          evaluation, reference, function(logic, "logic_rt_lock"), function(logic, "logic_rt_unlock"),
          tick, function(diagnostics, "diagnostics_device_link_state"), function(scheduler, "pollNextJbc"),
          function(scheduler, "readNextJbcState")]
Path(sys.argv[1]).write_text("\n\n".join(pieces), encoding="utf-8")
if len(sys.argv) > 2:
    cpu = (root / "MasterDiagnostics.inc.h").read_text(encoding="utf-8")
    Path(sys.argv[2]).write_text("\n\n".join(function(cpu, name) for name in
        ("sample_cpu_load", "append_cpu_load_json")), encoding="utf-8")
if len(sys.argv) > 3:
    Path(sys.argv[3]).write_text(cpu[cpu.index('static ofe_runtime::PhaseWindow master_runtime_window;'):], encoding='utf-8')
if len(sys.argv) > 4:
    status = (root / "WebStatus.inc.h").read_text(encoding="utf-8")
    Path(sys.argv[4]).write_text(function(status, "json_escape"), encoding="utf-8")
if len(sys.argv) > 5:
    def class_block(name):
        start = cpu.index("class " + name + " {")
        return cpu[start:cpu.index("\n};", start) + 3]
    names = ("mqtt_perf_tick", "mqtt_perf_record", "mqtt_perf_snapshot",
             "mqtt_transport_probe_enabled", "mqtt_transport_probe_record", "http_perf_record")
    pieces = [function(cpu, name) for name in names]
    pieces += [class_block("MqttPerfScope"), class_block("HttpPerfScope"), function(cpu, "append_http_perf_json")]
    Path(sys.argv[5]).write_text("\n\n".join(pieces), encoding="utf-8")
if len(sys.argv) > 6:
    mqtt = (root / "MasterMqtt.inc.h").read_text(encoding="utf-8")
    defs = function(status, "append_universal_entity_defs_json_uncached")
    defs = defs.replace('  json +=', '  ++defs_builds;\n  json +=', 1)
    pieces = [function(status, "entity_span_string"), defs,
              function(status, "append_universal_entity_defs_json"),
              function(mqtt, "ha_route_metadata_key"), function(mqtt, "ha_append_route_options")]
    Path(sys.argv[6]).write_text("\n\n".join(pieces), encoding="utf-8")
if len(sys.argv) > 7:
    Path(sys.argv[7]).write_text("\n\n".join(function(scheduler, name) for name in
        ("requestScanKnownModules", "requestStartupScan", "pollStartupScan", "startScanJob", "pollScanJob")),
        encoding="utf-8")
if len(sys.argv) > 8:
    power = (root / "ModulePowerSaveState.h").read_text(encoding="utf-8")
    pieces = [function(power, "module_update_uptime")]
    pieces += [function(scheduler, name) for name in
               ("setModulePowerSave", "wakeModule", "wakeAllModules", "refreshPowerSaveActive", "powerSaveIdle", "updatePowerSave")]
    Path(sys.argv[8]).write_text("\n\n".join(pieces), encoding="utf-8")

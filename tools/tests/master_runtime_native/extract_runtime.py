"""Build native fixtures from the actual master functions, not copies of them."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3] / "OpenFumeExtractorMaster/src"
logic = (root / "WebLogic.inc.h").read_text(encoding="utf-8")


def function(source, name):
    match = re.search(r"^(?:static |bool MasterScheduler::)[^\n]*\b" + name + r"\([^\n]*\) \{", source, re.M)
    if not match:
        raise RuntimeError(name)
    pos = source.index("{", match.start())
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


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
          tick, function(diagnostics, "diagnostics_device_link_state"), function(scheduler, "pollNextJbc")]
Path(sys.argv[1]).write_text("\n\n".join(pieces), encoding="utf-8")

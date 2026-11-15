from pathlib import Path

root = Path(__file__).resolve().parents[3]
source = (root / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebStatus.inc.h').read_text(encoding='utf-8-sig')
start = source.index('static void apply_module_labels() {')
end = source.index('\n}\n', start) + 3
out = root / 'tools/tests/build-checks/module-labels-native'
out.mkdir(parents=True, exist_ok=True)
(out / 'apply_labels.h').write_text(source[start:end], encoding='utf-8')
source = (root / 'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/ModuleRegistry.cpp').read_text(encoding='utf-8-sig')
start = source.index('void ModuleRegistry::mergeHistory(')
end = source.index('\n}\n', start) + 3
(out / 'merge_labels.h').write_text(source[start:end], encoding='utf-8')

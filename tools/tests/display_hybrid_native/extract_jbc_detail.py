import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
source = (root / 'OpenFumeExtractorMaster/src/MasterScheduler.cpp').read_text(encoding='utf-8')
output = []
for name in ('consumeDisplayJbcExtension', 'sendDisplayJbcPage'):
    start = source.index('bool MasterScheduler::' + name + '(')
    end = source.index('\n}\n', start) + 3
    output.append(source[start:end].replace('MasterScheduler::', 'TestMaster::', 1))
Path(sys.argv[1]).write_text('\n'.join(output), encoding='utf-8')

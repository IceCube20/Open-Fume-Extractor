"""Extract production tunnel handlers; adapt only the socket send in the fixture."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
master = (root / 'OpenFumeExtractorMaster/src/MasterDisplayWifi.cpp').read_text(encoding='utf-8')
display = (root / 'Module/DisplayModule_800x480/src/OfeDisplayWifi.h').read_text(encoding='utf-8')


def function(source, anchor):
    start = source.index(anchor)
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


parts = [function(master, 'bool MasterDisplayWifi::send(').replace('sendPacket(', 'capturePacket(')]
parts.append(function(master, 'void MasterDisplayWifi::responseProcessed('))
receive = function(master, 'bool MasterDisplayWifi::receive(')
master_poll = next(line for line in receive.splitlines() if 'peer.timing.polled(received_us)' in line)
tail = receive[receive.index('  if (!p->connected || !samePeer'): -1]
parts.append('''bool MasterDisplayWifi::deliver(const Packet& msg, Frame& frame, uint32_t received_us) {
  Peer* p = &peers_[0]; const auto from = p->endpoint; const uint32_t now = millis();
''' + master_poll + '\n' + tail + '}')
send = function(display, '  bool send(uint8_t kind,')
send = send.replace('  bool send(', 'bool OfeDisplayWifi::send(')
send = send.replace('const jbc_rs485::Frame* frame=nullptr', 'const jbc_rs485::Frame* frame')
parts.append(send.replace('sendPacket(', 'capturePacket('))
poll = function(display, '  bool poll(jbc_rs485::Frame& frame)')
display_poll = next(line for line in poll.splitlines() if 'timing_rx_polls_.sample(received_us)' in line)
tail = poll[poll.index('    Packet p;'): -1]
parts.append('''bool OfeDisplayWifi::deliver(const uint8_t* raw, size_t n, uint32_t received_us, Frame& frame) {
  using namespace ofe_wifi;
''' + display_poll + '\n' + tail + '}')
Path(sys.argv[1]).write_text('\n'.join(parts), encoding='utf-8')

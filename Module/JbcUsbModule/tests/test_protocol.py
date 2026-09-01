DLE = 0x10
STX = 0x02
ETX = 0x03


def encode(protocol, source, target, cmd, data=b'', fid=0, response=False):
    assert protocol in (1, 2)
    assert len(data) <= 255
    src = source ^ 0x80 if response else source
    logical = bytearray([STX, src, target])
    if protocol == 2:
        logical.append(fid)
    logical += bytearray([cmd, len(data)]) + bytearray(data) + bytearray([0, ETX])
    bcc = 0
    for b in logical:
        bcc ^= b
    logical[-2] = bcc

    wire = bytearray([DLE, STX])
    for b in logical[1:-1]:
        wire.append(b)
        if b == DLE:
            wire.append(DLE)
    wire += bytes([DLE, ETX])
    return bytes(logical), bytes(wire)


def decode(wire, protocol=None):
    assert wire[:2] == bytes([DLE, STX])
    assert wire[-2:] == bytes([DLE, ETX])
    logical = bytearray([STX])
    i = 2
    while i < len(wire) - 2:
        b = wire[i]
        i += 1
        if b == DLE:
            assert i < len(wire) - 2 and wire[i] == DLE
            i += 1
        logical.append(b)
    logical.append(ETX)

    x = 0
    for b in logical:
        x ^= b
    assert x == 0

    p01 = len(logical) >= 7 and logical[4] + 7 == len(logical)
    p02 = len(logical) >= 8 and logical[5] + 8 == len(logical)
    if protocol == 1:
        assert p01
    elif protocol == 2:
        assert p02
    else:
        assert p01 or p02
    return bytes(logical)


# Known DLL-derived P02 active-handshake vector.
logical2, wire2 = encode(2, 0x19, 0x10, 0x00, b'\x10', fid=0xFD)
assert logical2 == bytes.fromhex('02 19 10 FD 00 01 10 E4 03'), logical2.hex(' ')
assert wire2 == bytes.fromhex('10 02 19 10 10 FD 00 01 10 10 E4 10 03'), wire2.hex(' ')
assert decode(wire2, 2) == logical2

# P01 firmware request once the station supplied host/source address 0x19.
logical1, wire1 = encode(1, 0x19, 0x00, 0x21)
assert logical1 == bytes.fromhex('02 19 00 21 00 39 03'), logical1.hex(' ')
assert wire1 == bytes.fromhex('10 02 19 00 21 00 39 10 03'), wire1.hex(' ')
assert decode(wire1, 1) == logical1

# Source bit 7 marks a response in both protocol generations.
resp1, _ = encode(1, 0x00, 0x19, 0x21, b'01:DDE:1:2', response=True)
resp2, _ = encode(2, 0x10, 0x19, 0x21, b'02:DME:1:2', fid=0xED, response=True)
assert resp1[1] == 0x80
assert resp2[1] == 0x90

# DLE stuffing must work for data and for a BCC which happens to equal DLE.
for protocol in (1, 2):
    logical, wire = encode(protocol, 0x19, 0x10, 0x30, b'\x10', fid=0x22)
    assert bytes([DLE, DLE]) in wire[2:-2]
    assert decode(wire, protocol) == logical

    found_bcc_dle = False
    for v in range(256):
        logical, wire = encode(protocol, 0x19, 0x10, 0x30, bytes([v]), fid=0x22)
        if logical[-2] == DLE:
            assert bytes([DLE, DLE]) in wire[2:-2]
            assert decode(wire, protocol) == logical
            found_bcc_dle = True
            break
    assert found_bcc_dle



# EncodeFrame.LENGTH_FRAME is LENGTH_CONTROL + byte.MaxValue: full 255-byte
# payloads must round-trip in both frame protocol generations.
max_payload = bytes(range(255))
for protocol in (1, 2):
    logical, wire = encode(protocol, 0x19, 0x10, 0x30, max_payload, fid=0xEF)
    assert len(logical) == (262 if protocol == 1 else 263)
    assert logical[4 if protocol == 1 else 5] == 255
    assert decode(wire, protocol) == logical

# EncodeFrame toggles the response bit with XOR (rather than forcing it high).
resp_toggle, _ = encode(2, 0x99, 0x90, 0x30, b'', fid=1, response=True)
assert resp_toggle[1] == 0x19
assert resp_toggle[2] == 0x90

# Port table reconstructed from CStationsConfiguration in the supplied JBC library.
MODEL_PORTS = {
    'CA': 1, 'CDCF': 1, 'CDN': 1, 'CP': 1, 'CSCV': 1, 'CDE': 1,
    'CFE': 1, 'CAE': 1, 'CPE': 1, 'CSVE': 1, 'DD': 2, 'DDE': 2, 'DDR': 2,
    'DI': 1, 'DM': 4, 'DME': 4, 'HD': 1, 'HDE': 1, 'HDR': 1,
    'LC': 1, 'NA': 2, 'NAE': 2, 'PSE': 4, 'SM': 1, 'WS': 1,
    'ALE': 1, 'JT': 1, 'JTSE': 1, 'SF': 1, 'F1': 1, 'F2W': 2,
    'F2': 2, 'F4W': 4, 'PH': 1, 'PHBE': 1, 'PHNE': 1, 'PHSE': 1,
    'PHXL': 1, 'CLM': 1,
}
assert MODEL_PORTS['DDE'] == 2
assert MODEL_PORTS['DME'] == 4
assert MODEL_PORTS['F4W'] == 4
assert MODEL_PORTS['CDCF'] == 1
assert max(MODEL_PORTS.values()) == 4

print('JBC USB P01/P02 codec vectors: OK')
print('P02 handshake logical:', logical2.hex(' ').upper())
print('P02 handshake wire   :', wire2.hex(' ').upper())
print('P01 firmware logical :', logical1.hex(' ').upper())
print('P01 firmware wire    :', wire1.hex(' ').upper())
print('Known model entries  :', len(MODEL_PORTS))

# Station class map reconstructed from CStationsConfiguration / StationFactory.
MODEL_TYPES = {}
for m in ['CA','CDCF','CDN','CP','CSCV','CDE','CFE','CAE','CPE','CSVE','DD','DDE','DDR','DI','DM','DME','HD','HDE','HDR','LC','NA','NAE','PSE','SM','WS','ALE']:
    MODEL_TYPES[m] = 'SOLD'
for m in ['JT','JTSE']:
    MODEL_TYPES[m] = 'HA'
MODEL_TYPES['SF'] = 'SF'
for m in ['F1','F2W','F2','F4W']:
    MODEL_TYPES[m] = 'FE'
for m in ['PH','PHBE','PHNE','PHSE','PHXL']:
    MODEL_TYPES[m] = 'PH'
MODEL_TYPES['CLM'] = 'CL'
assert set(MODEL_TYPES) == set(MODEL_PORTS)
assert MODEL_TYPES['JTSE'] == 'HA'
assert MODEL_TYPES['DME'] == 'SOLD'
assert MODEL_TYPES['PHSE'] == 'PH'

# Original FEAutoWorking rules from JBC_Connect.dll.
def auto_work(kind, *, tool=0, stand=False, sleep=False, hibernation=False,
              extractor=False, heater=False):
    if kind == 'SOLD':
        return tool != 0 and not (stand or sleep or hibernation or extractor)
    if kind == 'HA':
        return tool != 0 and heater
    if kind == 'PH':
        return heater
    return False

assert auto_work('SOLD', tool=2)
assert not auto_work('SOLD', tool=2, stand=True)
assert not auto_work('SOLD', tool=2, sleep=True)
assert not auto_work('SOLD', tool=2, hibernation=True)
assert not auto_work('SOLD', tool=2, extractor=True)
assert not auto_work('SOLD', tool=0)
assert auto_work('HA', tool=2, heater=True)
assert not auto_work('HA', tool=2, heater=False)
assert not auto_work('HA', tool=0, heater=True)
assert auto_work('PH', heater=True)
assert not auto_work('PH', heater=False)
for kind in ('SF', 'FE', 'CL', 'UNKNOWN'):
    assert not auto_work(kind, tool=2, heater=True)

print('JBC station classes / original auto-working rules: OK')

# DLL-fidelity regression: P02 SOLD InfoPort bit5 and ToolLastStatus bit5 are
# deliberately different concepts in the original implementation.
def decode_sold_info_status(raw, qst=True):
    return {
        'stand': bool(raw & 0x01),
        'sleep': bool(raw & 0x02),
        'hibernation': bool(raw & 0x04),
        'extractor': bool(raw & 0x08),
        'desolder': bool(raw & 0x10),
        'enabled_port': (not bool(raw & 0x20)) if qst else None,
    }

def decode_sold_tool_last_status(raw):
    return {
        'stand': bool(raw & 0x01),
        'sleep': bool(raw & 0x02),
        'hibernation': bool(raw & 0x04),
        'extractor': bool(raw & 0x08),
        'desolder': bool(raw & 0x10),
        'qst_lock': bool(raw & 0x20),
        'active_cleaning': bool(raw & 0x40),
    }

info = decode_sold_info_status(0x20)
tool_status = decode_sold_tool_last_status(0x20)
assert info['enabled_port'] is False
assert tool_status['qst_lock'] is True
assert decode_sold_info_status(0x00)['enabled_port'] is True
assert decode_sold_tool_last_status(0x40)['active_cleaning'] is True

# k20/k26 request/response shape verified in JBC_Connect: request {port},
# response {raw ToolStatus, port}.
tool_req_logical, tool_req_wire = encode(2, 0x19, 0x10, 0x57, bytes([1]), fid=0x22)
assert tool_req_logical[4] == 0x57 and tool_req_logical[5] == 1 and tool_req_logical[6] == 1
assert decode(tool_req_wire, 2) == tool_req_logical
print('JBC SOLD QST/EnabledPort/ToolLastStatus fidelity vectors: OK')


# DLL-fidelity regression: ReadLockPort and JBC continuous telemetry remain
# separate from OFE suction mode. 0.1.27 additionally starts/consumes the JBC
# stream like JBC_Connect while keeping M_INF_PORT in parallel.
def decode_p02_lock_port(raw):
    # ReceiveFrame02_SOLD case 0x88: 0 => EnabledPort ON.
    return raw == 0

assert decode_p02_lock_port(0x00) is True
assert decode_p02_lock_port(0x01) is False

# M_R_CONTIMODE (0x80): byte0 speed, byte1 enabled-port mask. P02 may append
# a third byte in newer firmware; it does not change the first two fields.
def decode_conti_read(data, protocol):
    assert len(data) == 2 if protocol == 1 else len(data) in (2, 3)
    return data[0], data[1]

assert decode_conti_read(bytes([0, 0]), 1) == (0, 0)
assert decode_conti_read(bytes([1, 0x05]), 2) == (1, 0x05)
assert decode_conti_read(bytes([2, 0x03, 1]), 2) == (2, 0x03)

# Read/write layouts exactly match SendFrame01/02_SOLD. JBCStationsData's
# SpeedContinuousMode includes OFF=0, T_10mS=1 and T_100mS=4. The original
# DLL StartContinuousMode uses 10 ms. OFE 0.1.47 deliberately sends OFF=0
# for SOLD as an A/B stability profile while HA remains at 10 ms. The normal
# write layout is identical for OFF and all supported speed values.
lock2_logical, lock2_wire = encode(2, 0x19, 0x10, 0x88, bytes([1]), fid=0x31)
assert lock2_logical[4] == 0x88 and lock2_logical[5] == 1 and lock2_logical[6] == 1
assert decode(lock2_wire, 2) == lock2_logical
lock1_logical, lock1_wire = encode(1, 0x19, 0x00, 0xD4, bytes([1]))
assert lock1_logical[3] == 0xD4 and lock1_logical[4] == 1 and lock1_logical[5] == 1
assert decode(lock1_wire, 1) == lock1_logical
conti2_logical, conti2_wire = encode(2, 0x19, 0x10, 0x80, b'', fid=0x32)
assert decode(conti2_wire, 2) == conti2_logical
conti_start2_logical, conti_start2_wire = encode(2, 0x19, 0x10, 0x81, bytes([1, 0x03]), fid=0x33)
assert conti_start2_logical[4] == 0x81 and conti_start2_logical[5] == 2
assert conti_start2_logical[6:8] == bytes([1, 0x03])
assert decode(conti_start2_wire, 2) == conti_start2_logical
conti_sold100_logical, conti_sold100_wire = encode(2, 0x19, 0x10, 0x81, bytes([4, 0x03]), fid=0x34)
assert conti_sold100_logical[6:8] == bytes([4, 0x03])
assert decode(conti_sold100_wire, 2) == conti_sold100_logical
conti_stop2_logical, conti_stop2_wire = encode(2, 0x19, 0x10, 0x81, bytes([0, 0x03]), fid=0x35)
assert conti_stop2_logical[6:8] == bytes([0, 0x03])
assert decode(conti_stop2_wire, 2) == conti_stop2_logical
conti_start1_logical, conti_start1_wire = encode(1, 0x19, 0x00, 0x81, bytes([1, 0x03]))
assert conti_start1_logical[3] == 0x81 and conti_start1_logical[4] == 2
assert conti_start1_logical[5:7] == bytes([1, 0x03])
assert decode(conti_start1_wire, 1) == conti_start1_logical

# Non-ALE P02 M_I_CONTIMODE: sequence + 10 bytes per enabled port.
# Temperature/power channel B is averaged only for PA(3)/HT(4).
def decode_p02_sold_conti(data, ports_mask, tools):
    ports = [p for p in range(4) if ports_mask & (1 << p)]
    assert len(data) == 1 + 10 * len(ports)
    out = {}
    for slot, port in enumerate(ports):
        base = 1 + 10 * slot
        ta = int.from_bytes(data[base:base+2], 'little')
        tb = int.from_bytes(data[base+2:base+4], 'little')
        pa = int.from_bytes(data[base+4:base+6], 'little')
        pb = int.from_bytes(data[base+6:base+8], 'little')
        status = data[base+8]
        dual = tools.get(port, 0) in (3, 4)
        out[port] = {
            'temp': (ta + tb) // 2 if dual else ta,
            'power': (pa + pb) // 2 if dual else pa,
            'stand': bool(status & 0x01),
            'sleep': bool(status & 0x02),
            'hibernation': bool(status & 0x04),
            'extractor': bool(status & 0x08),
            'desolder': bool(status & 0x10),
            'raw_bit5': bool(status & 0x20),
            'soldering': bool(status & 0x40),
            'calibrating': bool(status & 0x80),
        }
    return out

frame = bytearray([0x5A])
# Port0 T245: channel A wins; bit5 must stay raw and must NOT imply stand/QST.
frame += (350).to_bytes(2,'little') + (777).to_bytes(2,'little')
frame += (420).to_bytes(2,'little') + (999).to_bytes(2,'little')
frame += bytes([0x20 | 0x40, 0x00])
# Port1 PA: average both heaters; Sleep + Desolder + Calibrating.
frame += (300).to_bytes(2,'little') + (320).to_bytes(2,'little')
frame += (400).to_bytes(2,'little') + (600).to_bytes(2,'little')
frame += bytes([0x02 | 0x10 | 0x80, 0x00])
cm = decode_p02_sold_conti(bytes(frame), 0x03, {0:2, 1:3})
assert cm[0]['temp'] == 350 and cm[0]['power'] == 420
assert cm[0]['raw_bit5'] and not cm[0]['stand'] and cm[0]['soldering']
assert cm[1]['temp'] == 310 and cm[1]['power'] == 500
assert cm[1]['sleep'] and cm[1]['desolder'] and cm[1]['calibrating']

print('JBC ReadLockPort / continuous-mode merge fidelity vectors: OK')

# SOLD completion regression added in module 0.1.26.
# UpdateData_SOLD reads MOS temperature per port and transformer temperature;
# station parameters read the USB CONTROL/MONITOR mode. P02 k20/k26 also expose
# read-only tool type / last error / alarm configuration diagnostics.
mos1_logical, mos1_wire = encode(1, 0x19, 0x00, 0x58, bytes([1]))
assert mos1_logical[3] == 0x58 and mos1_logical[4] == 1 and mos1_logical[5] == 1
assert decode(mos1_wire, 1) == mos1_logical
mos2_logical, mos2_wire = encode(2, 0x19, 0x10, 0x59, bytes([1]), fid=0x40)
assert mos2_logical[4] == 0x59 and mos2_logical[5] == 1 and mos2_logical[6] == 1
assert decode(mos2_wire, 2) == mos2_logical
trafo2_logical, trafo2_wire = encode(2, 0x19, 0x10, 0xAF, b'', fid=0x41)
assert decode(trafo2_wire, 2) == trafo2_logical
connect1_logical, connect1_wire = encode(1, 0x19, 0x00, 0x1E, b'')
connect2_logical, connect2_wire = encode(2, 0x19, 0x10, 0xE0, b'', fid=0x42)
assert decode(connect1_wire, 1) == connect1_logical
assert decode(connect2_wire, 2) == connect2_logical

# k20/k26 read-only request layouts are all {port}; replies return either
# {value, port} (0x55/0x56) or {int16 temp, int16 delay, port} (0x83/0x85).
for cmd in (0x55, 0x56, 0x83, 0x85):
    logical, wire = encode(2, 0x19, 0x10, cmd, bytes([2]), fid=(0x50 + (cmd & 0x0F)))
    assert logical[4] == cmd and logical[5] == 1 and logical[6] == 2
    assert decode(wire, 2) == logical

def decode_alarm_cfg(data):
    assert len(data) == 5 and data[4] < 4
    temp = int.from_bytes(data[0:2], 'little', signed=True)
    delay = int.from_bytes(data[2:4], 'little', signed=True)
    return temp, delay, data[4]

assert decode_alarm_cfg(bytes([0x34, 0x12, 0x0A, 0x00, 1])) == (0x1234, 10, 1)

# D8/v1 is exactly 24 bytes and can coexist with the maximum telemetry prefix
# and the 16-byte D6 station-name suffix when sent as the alternating frame.
d8 = bytearray([0xD8, 1, 2, 0x1F])
d8 += (300).to_bytes(2, 'little')
d8 += bytes([2, 3])
d8 += (400).to_bytes(2, 'little', signed=True)
d8 += (15).to_bytes(2, 'little', signed=True)
d8 += (180).to_bytes(2, 'little', signed=True)
d8 += (20).to_bytes(2, 'little', signed=True)
d8 += bytes([0x0F])
d8 += (350).to_bytes(2, 'little')
d8 += (500).to_bytes(2, 'little')
d8 += (450).to_bytes(2, 'little')
d8 += bytes([1])
assert len(d8) == 24
assert 99 + len(d8) + (3 + 16) <= 192
assert 99 + (3 + 60 + 2) + (3 + 16) <= 192

# 0x87 is deliberately not in the automatic diagnostic poll table because the
# original API names/handles it as read-and-clear (M_R_ALARMTEMPSET_NCLEAR).
from pathlib import Path
source = (Path(__file__).resolve().parents[1] / 'JbcUsbModule.ino').read_text()
diag_anchor = 'static const uint8_t cmds[] = {JBC_CMD_TOOL_TYPE_SOLD, JBC_CMD_TOOL_LAST_ERROR_SOLD,'
diag_start = source.index(diag_anchor)
diag_end = source.index('};', diag_start)
diag_table = source[diag_start:diag_end]
assert 'JBC_CMD_ALARM_TRIGGER_NCLEAR_SOLD' not in diag_table
assert 'JBC_CMD_ALARM_MAX_SOLD' in diag_table and 'JBC_CMD_ALARM_MIN_SOLD' in diag_table
assert 'next_sold_telemetry_stage = 0; // D7,D8,DA,DB,D3(peripheral metadata),DD,DC,DF,D5(ALE),D4(ALE/CDE extended counters)' in source
assert 'resp.payload[o++]=0xD8' in source

print('JBC SOLD MOS/Trafo/connect/diagnostic/D8 fidelity vectors: OK')


# HOT_AIR/JT/JTSE fidelity regression added in module 0.1.28.
# P02 M_I_CONTIMODE is sequence + 14 bytes per enabled port:
# temp, flow, power, extTC1, extTC2, TimeToStop, raw ToolStatus_HA, reserved.
def decode_p02_ha_conti(data, ports_mask):
    ports = [p for p in range(4) if ports_mask & (1 << p)]
    assert len(data) == 1 + 14 * len(ports)
    out = {}
    for slot, port in enumerate(ports):
        base = 1 + 14 * slot
        ext1 = int.from_bytes(data[base+6:base+8], 'little')
        ext2 = int.from_bytes(data[base+8:base+10], 'little')
        status = data[base+12]
        out[port] = {
            'temp': int.from_bytes(data[base:base+2], 'little'),
            'flow': int.from_bytes(data[base+2:base+4], 'little'),
            'power': int.from_bytes(data[base+4:base+6], 'little'),
            'ext1': 0 if ext1 == 0xFFFF else ext1,
            'ext2': 0 if ext2 == 0xFFFF else ext2,
            'time_to_stop': int.from_bytes(data[base+10:base+12], 'little'),
            'heater': bool(status & 0x01),
            'heater_requested': bool(status & 0x02),
            'cooling': bool(status & 0x04),
            'suction': bool(status & 0x08),
            'suction_requested': bool(status & 0x10),
            'pedal_connected': bool(status & 0x20),
            'pedal_pressed': bool(status & 0x40),
            'stand': bool(status & 0x80),
            'raw_status': status,
        }
    return out

ha = bytearray([0x27])
ha += (510).to_bytes(2, 'little')     # 51.0 C in JBC UTI example
ha += (600).to_bytes(2, 'little')     # 60.0 % flow
ha += (24).to_bytes(2, 'little')      # 2.4 % power
ha += (0xFFFF).to_bytes(2, 'little')  # disconnected ext TC1 -> 0
ha += (123).to_bytes(2, 'little')     # ext TC2
ha += (240).to_bytes(2, 'little')     # 4 min TimeToStop
ha += bytes([0x80 | 0x20, 0x00])      # STAND + pedal connected, reserved
hv = decode_p02_ha_conti(bytes(ha), 0x01)[0]
assert hv['temp'] == 510 and hv['flow'] == 600 and hv['power'] == 24
assert hv['ext1'] == 0 and hv['ext2'] == 123 and hv['time_to_stop'] == 240
assert hv['stand'] and hv['pedal_connected'] and not hv['heater']

# HA uses the same Read/WriteContiMode commands as SOLD, but P02 only.
ha_conti_read, ha_conti_read_wire = encode(2, 0x19, 0x10, 0x80, b'', fid=0x61)
ha_conti_start, ha_conti_start_wire = encode(2, 0x19, 0x10, 0x81, bytes([1, 0x01]), fid=0x62)
assert decode(ha_conti_read_wire, 2) == ha_conti_read
assert decode(ha_conti_start_wire, 2) == ha_conti_start
assert ha_conti_start[6:8] == bytes([1, 0x01])

# HA station ConnectStatus is P02 USB 0xE0 and is a station value, not a port value.
ha_connect, ha_connect_wire = encode(2, 0x19, 0x10, 0xE0, b'', fid=0x63)
assert decode(ha_connect_wire, 2) == ha_connect

print('JBC HOT_AIR continuous/ConnectStatus fidelity vectors: OK')

# HOT_AIR diagnostics/counters added in module 0.1.29.
# Original SendFrame02_HA uses one-byte {port} requests for direct reads and
# global/partial counters; station diagnostics use empty payloads.
for cmd in (0x35, 0x37, 0x52, 0x54, 0x55, 0x56, 0x57, 0x5D, 0xC0, 0xC2, 0xC4, 0xC6, 0xD0, 0xD2, 0xD4, 0xD6):
    logical, wire = encode(2, 0x19, 0x10, cmd, bytes([0]), fid=(0x70 + (cmd & 0x0F)) & 0xFF)
    assert logical[4] == cmd and logical[5] == 1 and logical[6] == 0
    assert decode(wire, 2) == logical
for cmd in (0x60, 0x9A, 0xA0, 0xA2, 0xA4, 0xA6, 0xF0, 0xF2):
    logical, wire = encode(2, 0x19, 0x10, cmd, b'', fid=(0x80 + (cmd & 0x0F)) & 0xFF)
    assert logical[4] == cmd and logical[5] == 0
    assert decode(wire, 2) == logical

# D9/v1 worst-case size: marker/version/port + direct diagnostics + four u24
# partial counters + station diagnostics + 7-byte robot config + 12-byte profile.
d9 = bytearray([0xD9, 1, 0])
d9 += (0x007F).to_bytes(2, 'little')
d9 += (510).to_bytes(2, 'little') + (24).to_bytes(2, 'little') + (600).to_bytes(2, 'little')
d9 += bytes([2, 0, 0x80, 2, 1])
for v in (10000, 2500, 123, 45): d9 += int(v).to_bytes(3, 'little')
d9 += (0x00FF).to_bytes(2, 'little')
d9 += bytes([0, 0])
for v in (4050, 1350, 1000, 100, 4050, 225): d9 += int(v).to_bytes(2, 'little')
d9 += bytes([3, ord('8'), ord('N'), 1, 0, ord('0'), ord('1')])
d9 += bytes([0])
profile = b'PROFILE_0001'
d9 += bytes([len(profile)]) + profile
assert len(d9) == 65
assert 99 + len(d9) + (3 + 16) <= 192

print('JBC HOT_AIR direct/partial/station diagnostic D9 fidelity vectors: OK')


# SOLD completion regression added in module 0.1.33.
# P02 k20 partial counters are individual D0..DC reads with {port}; k26 uses
# grouped C2. Station parameters mirror UpdateData_SOLD and are read-only.
for cmd in (0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC):
    logical, wire = encode(2, 0x19, 0x10, cmd, bytes([1]), fid=(0x90 + (cmd & 0x0F)) & 0xFF)
    assert logical[4] == cmd and logical[5] == 1 and logical[6] == 1
    assert decode(wire, 2) == logical
k26_partial, k26_partial_wire = encode(2, 0x19, 0x10, 0xC2, bytes([1]), fid=0xA2)
assert decode(k26_partial_wire, 2) == k26_partial
for cmd in (0xA2, 0xA4, 0xA8, 0xAC, 0xF0, 0xF2, 0xF9):
    logical, wire = encode(2, 0x19, 0x10, cmd, b'', fid=(0xA0 + (cmd & 0x0F)) & 0xFF)
    assert logical[4] == cmd and logical[5] == 0
    assert decode(wire, 2) == logical
for cmd in (0xFA, 0xFC):
    logical, wire = encode(2, 0x19, 0x10, cmd, bytes([2]), fid=(0xB0 + (cmd & 0x0F)) & 0xFF)
    assert logical[4] == cmd and logical[5] == 1 and logical[6] == 2
    assert decode(wire, 2) == logical
profile, profile_wire = encode(2, 0x19, 0x10, 0x9A, b'jpf\x01', fid=0xBA)
assert profile[6:10] == b'jpf\x01' and decode(profile_wire, 2) == profile

# DA/v1 worst-case port completion record (12-byte profile) and DB/v1 station
# record (PIN + four compact peripheral records) must both fit beside a maximum
# 32-byte Device-ID prefix and 16-byte D6 station name. D3/v1 carries the
# additional 18-byte Hash_MCU_UID/DateTime tuple per peripheral on its own
# telemetry round. The temporary raw 0xFA D2/v1 diagnostic is removed again.
da_len = 33 + 12
db_len = 21 + 4 * 8
d3_len = 3 + 4 * 18
assert 99 + da_len + (3 + 16) <= 192
assert 99 + db_len + (3 + 16) <= 192
assert 99 + d3_len <= 192
assert 'resp.payload[o++]=0xDA' in source and 'resp.payload[o++]=0xDB' in source and 'resp.payload[o++]=0xD3' in source
assert 'JBC_CMD_COUNTER_PLUG_PARTIAL_SOLD' in source
assert 'sold_supports_partial_counters()' in source
assert 'JBC_CMD_PIN_SOLD' in source and 'jbc_sold_pin[5]' in source
assert 'JBC_CMD_PERIPHERAL_CONFIG_SOLD' in source and 'JBC_CMD_PERIPHERAL_STATUS_SOLD' in source
assert 'JBC_CMD_SELECTED_PROFILE_SOLD' in source and 'JBC_CMD_ASSISTANT_CONFIG_SOLD' in source

# The raw PIN may travel module -> Master, but WebStatus must only publish it
# when developer mode is enabled.
root = Path(__file__).resolve().parents[3]
web = (root / 'OpenFumeExtractorMaster' / 'src' / 'WebStatus.inc.h').read_text()
master = (root / 'OpenFumeExtractorMaster' / 'src' / 'MasterScheduler.cpp').read_text()
registry = (root / 'OpenFumeExtractorMaster' / 'src' / 'ModuleRegistry.h').read_text()
assert 'developer_mode_enabled && (m.jbc_usb_sold_extra_station_flags & 0x0004U)' in web
assert 'jbc_usb_sold_pin' in web and 'SOLD JBC Stationsdaten' in web
assert 'resp.payload[q] == 0xDA' in master and 'resp.payload[q] == 0xDB' in master and 'resp.payload[q] == 0xD3' in master
assert 'jbc_usb_sold_pin[5]' in registry
assert 'hash_mcu_uid[5]' in registry and 'datetime[15]' in registry
assert 'Teil Eingesteckt' in web and 'Teil Desolder-Zyklen' in web

print('JBC SOLD partial/profile/assistant/station/peripheral DA/DB fidelity vectors: OK')

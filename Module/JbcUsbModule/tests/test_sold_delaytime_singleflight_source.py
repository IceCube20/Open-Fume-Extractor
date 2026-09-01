from pathlib import Path
src = (Path(__file__).resolve().parents[1] / 'JbcUsbModule.ino').read_text()

assert 'static bool next_sold_port_poll_delay_phase = false;' in src
start = src.index('if (!uid_provisioning && (int32_t)(now - next_port_poll_ms) >= 0)')
end = src.index('if (!uid_provisioning && jbc_station_kind == JBC_STATION_CL', start)
block = src[start:end]

# SOLD/P02 must sequence the two UpdateData reads instead of attempting both
# in the same single-flight pass.
assert 'if (!next_sold_port_poll_delay_phase)' in block
assert 'ok = jbc_send_info_port(polled_port);' in block
assert 'if (ok) next_sold_port_poll_delay_phase = true;' in block
assert 'ok = jbc_send_delay_time(polled_port);' in block
assert 'next_sold_port_poll_delay_phase = false;' in block
assert block.index('ok = jbc_send_info_port(polled_port);') < block.index('ok = jbc_send_delay_time(polled_port);')

# Do not advance a normal port when the global single-flight gate rejected it.
assert 'else if (ok)' in block
assert 'Do not skip this' in block

# The actual DelayTime decoder must still update the live countdown/future mode.
assert 'static void decode_delay_time(const JbcFrame& f)' in src
assert 'jbc_ports[port].time_to_sleep_hibern = countdown;' in src
assert 'jbc_ports[port].future_mode = future;' in src

print('SOLD P02 InfoPort/DelayTime single-flight sequencing: OK')

# Run against an attached client with matching symbols loaded:
# (gdb) source tools/arm64/check-nodebuilder-angles.gdb
# Calls actual node-builder conversion; no level or renderer state changes.
python
import gdb

cases = [
    (1, 0, 0x00000000),
    (1, 1, 0x20000000),
    (0, 1, 0x40000000),
    (-1, 1, 0x60000000),
    (-1, 0, 0x80000000),
    (-1, -1, 0xa0000000),
    (0, -1, 0xc0000000),
    (1, -1, 0xe0000000),
]
failures = []
for x, y, expected in cases:
    actual = int(gdb.parse_and_eval(
        'FNodeBuilder::PointToAngle(%d, %d)' % (x * 65536, y * 65536)))
    if actual != expected:
        failures.append('(%d, %d): got %08x, expected %08x' %
                        (x, y, actual, expected))
if failures:
    raise gdb.GdbError('Node-builder angle regression: ' + '; '.join(failures))
print('PASS: node-builder angles, all eight directions')
end

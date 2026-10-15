# Attach in an outdoor GL scene (e.g. Doom II MAP07).
# Use matching debug symbols. Reject duplicate sky keys, ignoring padding.
python
import gdb

# Wait until BSP traversal has prepared every visible sky for this frame.
gdb.Breakpoint("FGLRenderer::RenderScene", temporary=True, internal=True)
gdb.execute("continue")
array = gdb.parse_and_eval("UniqueSkies.Array")
count = int(array["Count"])
if count == 0:
    raise gdb.GdbError("No prepared skies; retry in an outdoor GL scene")
keys = set()
for index in range(count):
    sky = array["Array"][index].dereference()
    key = (
        float(sky["x_offset"][0]), float(sky["x_offset"][1]),
        float(sky["y_offset"]),
        int(sky["texture"][0]), int(sky["texture"][1]),
        int(sky["skytexno1"]["texnum"]),
        bool(sky["mirrored"]), bool(sky["doublesky"]), bool(sky["sky2"]),
        int(sky["fadecolor"]["d"]),
    )
    if key in keys:
        raise gdb.GdbError("FAIL: duplicate sky portal key at entry %d of %d" % (index, count))
    keys.add(key)
print("PASS: %d prepared sky keys, no duplicate portals" % count)
end

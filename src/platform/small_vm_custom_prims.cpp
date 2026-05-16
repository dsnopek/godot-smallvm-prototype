// small_vm_custom_prims.cpp — Application-specific MicroBlocks primitives,
// ported from thirdparty/smallvm/example/custom_prims.c to operate on a
// godot::Node2D set by the host.
//
// Same shape as the C version (a static PrimEntry table, registered into
// the BLEPrims slot via addPrimitiveSet) but each prim talks to the
// Node2D's transform instead of a plain HostObject struct. The block names
// match HostBlocks.ubl so existing scripts written against the example
// host run unchanged once the IDE-side library is added to the project.

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "small_vm_platform.h"

extern "C" {
#include "mem.h"
#include "interp.h"
}

using godot::Node2D;
using godot::Vector2;
using godot::UtilityFunctions;
using godot::String;

// The host (SmallVM::execute_bytecode) writes this before kicking the VM,
// then clears it on the way out. Primitives that operate on "the current
// object" read it directly.
static Node2D *s_current_object = nullptr;

extern "C" void host_set_current_object(Node2D *node) {
    s_current_object = node;
}

extern "C" Node2D *host_get_current_object(void) {
    return s_current_object;
}

// ---- Primitives ------------------------------------------------------------
//
// Each primitive must have C linkage so it slots into a PrimEntry table.
// Position values are stored as floats on Node2D; we truncate to int on
// the way out and accept ints on the way in.

extern "C" {

static OBJ prim_greet(int argCount, OBJ *args) {
    String name = "(no name)";
    if (argCount >= 1 && IS_TYPE(args[0], StringType)) {
        name = String::utf8(obj2str(args[0]));
    }
    UtilityFunctions::print("[SmallVM] hello, ", name, "!");
    return int2obj(0);
}

static OBJ prim_add(int argCount, OBJ *args) {
    if (argCount < 2) return fail(notEnoughArguments);
    if (!isInt(args[0]) || !isInt(args[1])) return fail(needsIntegerError);
    return int2obj(obj2int(args[0]) + obj2int(args[1]));
}

static OBJ prim_square(int argCount, OBJ *args) {
    if (argCount < 1) return fail(notEnoughArguments);
    int n = evalInt(args[0]);
    if (failure()) return falseObj; // evalInt already called fail()
    return int2obj(n * n);
}

static OBJ prim_setX(int argCount, OBJ *args) {
    if (!s_current_object) return fail(unspecifiedError);
    if (argCount < 1) return fail(notEnoughArguments);
    Vector2 p = s_current_object->get_position();
    p.x = (real_t) evalInt(args[0]);
    s_current_object->set_position(p);
    return int2obj(0);
}

static OBJ prim_setY(int argCount, OBJ *args) {
    if (!s_current_object) return fail(unspecifiedError);
    if (argCount < 1) return fail(notEnoughArguments);
    Vector2 p = s_current_object->get_position();
    p.y = (real_t) evalInt(args[0]);
    s_current_object->set_position(p);
    return int2obj(0);
}

static OBJ prim_moveBy(int argCount, OBJ *args) {
    if (!s_current_object) return fail(unspecifiedError);
    if (argCount < 2) return fail(notEnoughArguments);
    int dx = evalInt(args[0]);
    int dy = evalInt(args[1]);
    Vector2 p = s_current_object->get_position();
    p.x += (real_t) dx;
    p.y += (real_t) dy;
    s_current_object->set_position(p);
    return int2obj(0);
}

static OBJ prim_getX(int argCount, OBJ *args) {
    (void) argCount; (void) args;
    if (!s_current_object) return fail(unspecifiedError);
    return int2obj((int) s_current_object->get_position().x);
}

static OBJ prim_getY(int argCount, OBJ *args) {
    (void) argCount; (void) args;
    if (!s_current_object) return fail(unspecifiedError);
    return int2obj((int) s_current_object->get_position().y);
}

static PrimEntry entries[] = {
    {"greet",  prim_greet},
    {"add",    prim_add},
    {"square", prim_square},
    {"setX",   prim_setX},
    {"setY",   prim_setY},
    {"moveBy", prim_moveBy},
    {"getX",   prim_getX},
    {"getY",   prim_getY},
};

void addCustomHostPrims(void) {
    // We reuse the BLEPrims slot (index 9) so existing scripts written
    // against the example host (which target [ble:setX], etc.) run as-is.
    // See thirdparty/smallvm/example/custom_prims.c for the rationale.
    addPrimitiveSet(
        BLEPrims,
        "ble",
        sizeof(entries) / sizeof(PrimEntry),
        entries);
}

} // extern "C"

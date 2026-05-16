// custom_prims.c — Application-specific MicroBlocks primitives.
//
// This file is the template you copy into your own project when you want to
// expose host-side functionality to MicroBlocks scripts. Each primitive is
// a plain C function with the signature
//
//     OBJ myPrim(int argCount, OBJ *args);
//
// where:
//   - argCount is how many arguments the script passed
//   - args[0..argCount-1] are the argument OBJs (tagged 32-bit values)
//   - the return OBJ is what the script sees (use falseObj for "no value")
//
// You bundle the primitives into a "primitive set" identified by a name
// (matched at runtime by the IDE) and an index from the PrimitiveSetIndex
// enum in vm/interp.h.
//
// =========================================================================
// Picking a primitive-set index
// =========================================================================
// The IDE encodes primitive calls as
//
//     (6-bit primSetIndex, prim_name_string_literal, argCount)
//
// (see commandPrimitive_op in vm/interp.c). The set indices are fixed
// constants — see PrimitiveSetIndex in vm/interp.h — and the IDE's matching
// list lives in ide/MicroBlocksCompiler.gp (method initPrimsets). For this
// example we want to plug in our own blocks WITHOUT modifying any source
// files in vm/, so we reuse the BLEPrims slot (index 9). The IDE's name
// for that slot is "ble", so scripts that want to call us would use
//
//     call [ble:greet] "World"
//
// In your own project you have two options:
//
//   1. Keep reusing a slot you don't need (e.g. BLEPrims), and patch the
//      IDE's primSetNames list so the slot has a friendlier name (e.g.
//      change "ble" to "host"). This keeps vm/ unchanged.
//   2. Add a new entry to PrimitiveSetIndex in vm/interp.h (just before
//      PrimitiveSetCount) AND a matching entry to primSetNames in
//      ide/MicroBlocksCompiler.gp. This is the "officially supported"
//      extension point — see the comment above the enum.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "interp.h"

// -------- Example 1: a command that prints from C ---------------------------
// [host:greet name]
// Argument: a string (or anything that obj2str can render).
// Returns:  int2obj(0) — commands typically ignore the return value, but
//           returning a value is fine; the VM's POP_ARGS_COMMAND will drop it.
static OBJ prim_greet(int argCount, OBJ *args) {
    const char *name = "(no name)";
    if (argCount >= 1 && IS_TYPE(args[0], StringType)) {
        name = obj2str(args[0]);
    }
    printf("[script] hello, %s!\n", name);
    fflush(stdout);
    return int2obj(0);
}

// -------- Example 2: a reporter that returns a value -----------------------
// [host:add a b]  ->  a + b
// Arguments: two integers.
// Returns:   integer.
// `fail()` is the standard way to surface a typed runtime error to the
// script (it bubbles up to the IDE as a task error message).
static OBJ prim_add(int argCount, OBJ *args) {
    if (argCount < 2) return fail(notEnoughArguments);
    if (!isInt(args[0]) || !isInt(args[1])) return fail(needsIntegerError);
    int a = obj2int(args[0]);
    int b = obj2int(args[1]);
    return int2obj(a + b);
}

// -------- Example 3: a reporter that uses evalInt for coercion -------------
// [host:square n]  ->  n * n
// evalInt() will accept strings/byte-arrays that look like numbers (matches
// the loose typing used by most of the built-in primitives).
static OBJ prim_square(int argCount, OBJ *args) {
    if (argCount < 1) return fail(notEnoughArguments);
    int n = evalInt(args[0]);
    if (failure()) return falseObj; // evalInt called fail() for us
    return int2obj(n * n);
}

// ==========================================================================
// Passing host-side context to scripts: implicit "current object" pattern
// ==========================================================================
//
// Primitives are plain C functions — they can reach ANY host-side state via
// globals or statics. The pattern used here is the simplest one: the host
// sets a "current object" pointer before dispatching the script, and the
// primitives operate on that object implicitly.
//
// This works well when each script "belongs to" one host object (one VM
// task per sprite, for example). Scripts stay clean: `move by 5, -3`
// instead of `move object 0 by 5, -3`.
//
// We model a tiny "game" with up to 4 sprites, each with an (x, y)
// position. The host registers them, sets the current one before running,
// and primitives read/write through the C global.

#define MAX_HOST_OBJECTS 4

typedef struct {
    int active;
    int x;
    int y;
    char name[16];
} HostObject;

static HostObject host_objects[MAX_HOST_OBJECTS];
static int        host_current_object = -1;

// ----- Public C API (used by embed_main.cpp) -------------------------------

void host_register_object(int id, const char *name, int x, int y) {
    if (id < 0 || id >= MAX_HOST_OBJECTS) return;
    host_objects[id].active = 1;
    host_objects[id].x = x;
    host_objects[id].y = y;
    snprintf(host_objects[id].name, sizeof(host_objects[id].name), "%s", name);
}

void host_set_current_object(int id) {
    host_current_object = (id >= 0 && id < MAX_HOST_OBJECTS) ? id : -1;
}

int host_object_x(int id) {
    return (id >= 0 && id < MAX_HOST_OBJECTS) ? host_objects[id].x : 0;
}

int host_object_y(int id) {
    return (id >= 0 && id < MAX_HOST_OBJECTS) ? host_objects[id].y : 0;
}

// ----- Primitives (operate on the current object) --------------------------

static OBJ prim_setX(int argCount, OBJ *args) {
    if (host_current_object < 0) return fail(unspecifiedError);
    if (argCount < 1) return fail(notEnoughArguments);
    host_objects[host_current_object].x = evalInt(args[0]);
    return int2obj(0);
}

static OBJ prim_setY(int argCount, OBJ *args) {
    if (host_current_object < 0) return fail(unspecifiedError);
    if (argCount < 1) return fail(notEnoughArguments);
    host_objects[host_current_object].y = evalInt(args[0]);
    return int2obj(0);
}

static OBJ prim_moveBy(int argCount, OBJ *args) {
    if (host_current_object < 0) return fail(unspecifiedError);
    if (argCount < 2) return fail(notEnoughArguments);
    host_objects[host_current_object].x += evalInt(args[0]);
    host_objects[host_current_object].y += evalInt(args[1]);
    return int2obj(0);
}

static OBJ prim_getX(int argCount, OBJ *args) {
    if (host_current_object < 0) return fail(unspecifiedError);
    return int2obj(host_objects[host_current_object].x);
}

static OBJ prim_getY(int argCount, OBJ *args) {
    if (host_current_object < 0) return fail(unspecifiedError);
    return int2obj(host_objects[host_current_object].y);
}

// -------- Primitive set table -----------------------------------------------
//
// Each PrimEntry maps a name (the string the IDE sends in the compiled
// instruction) to the C function above. The array MUST be marked static
// (or otherwise have a lifetime that outlives the VM) — addPrimitiveSet
// stores the pointer rather than copying.

static PrimEntry entries[] = {
    {"greet",  prim_greet},
    {"add",    prim_add},
    {"square", prim_square},
    // Current-object accessors. The host sets the "current object" via
    // host_set_current_object() before running the script.
    {"setX",   prim_setX},
    {"setY",   prim_setY},
    {"moveBy", prim_moveBy},
    {"getX",   prim_getX},
    {"getY",   prim_getY},
};

void addCustomHostPrims(void) {
    addPrimitiveSet(
        BLEPrims,
        "ble", // IDE-side name; keep as "ble" so existing scripts can call us
               // without modifying ide/MicroBlocksCompiler.gp.
        sizeof(entries) / sizeof(PrimEntry),
        entries);
}

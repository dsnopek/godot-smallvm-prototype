#include "small_vm.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include "platform/small_vm_platform.h"

extern "C" {
#include "mem.h"
#include "interp.h"
#include "persist.h"
}

// The MicroBlocks VM keeps its entire state in static globals (object
// memory, chunk table, task array, primitive sets). It only supports one
// VM per process, so we initialize lazily and share that single instance
// across every SmallVM Godot object.
static bool s_vm_initialized = false;

static void ensure_vm_initialized() {
    if (s_vm_initialized) return;
    memInit();         // object memory + GC
    primsInit();       // registers vars/data/misc; other sets are
                       // empty stubs from small_vm_platform.c
    addCustomHostPrims();
    s_vm_initialized = true;
}

void SmallVM::_bind_methods() {
    godot::ClassDB::bind_method(D_METHOD("print_type", "variant"), &SmallVM::print_type);
    godot::ClassDB::bind_method(D_METHOD("execute_bytecode", "bytecode", "current_object"), &SmallVM::execute_bytecode);
}

void SmallVM::print_type(const Variant &p_variant) const {
    print_line(vformat("Type: %d", p_variant.get_type()));
}

void SmallVM::execute_bytecode(const PackedByteArray &p_bytecode, Node2D *p_current_object) {
    ensure_vm_initialized();

    // Point the VM's "code file" loader at our caller-supplied buffer.
    // The bytes are expected to be in the on-disk persistence format the
    // linux+pi VM writes to `ublockscode.bin`: a 4-byte 'S' cycle-header
    // word followed by persistent records.
    embed_set_bytecode(p_bytecode.ptr(), p_bytecode.size());

    // Designate the Node2D the script will move. Cleared on exit so a
    // dangling pointer doesn't outlive this call.
    host_set_current_object(p_current_object);

    // restoreScripts() runs initPersistentMemory + initCodeFile (which
    // copies our buffer into the VM's flash) + updateChunkTable, leaving
    // chunks[] populated. startAll() fires any "when started" hat tasks.
    // runTasksUntilDone() then drives the interpreter until every task is
    // idle. The whole call is synchronous — long-running scripts will
    // block this thread, so design scripts to terminate (or add stepping).
    restoreScripts();
    startAll();
    runTasksUntilDone();

    host_set_current_object(nullptr);
    // Leave embed_bytecode_data pointing at p_bytecode.ptr() momentarily;
    // it won't be reread until the next execute_bytecode call sets it again.
}

#pragma once

#include <stdint.h>

#ifdef __cplusplus
namespace godot {
class Node2D;
}

extern "C" {
#endif

// Defined in small_vm_platform.c. Points the VM's persistent code store at a
// caller-supplied bytecode buffer; the buffer must remain valid through the
// next restoreScripts() call.
void embed_set_bytecode(const uint8_t *data, int size);

// Defined in small_vm_custom_prims.cpp. Installs the "Host Blocks" primitive
// set into the BLEPrims slot. Must be called after primsInit().
void addCustomHostPrims(void);

#ifdef __cplusplus
}  // extern "C"

// Defined with C linkage in small_vm_custom_prims.cpp but operate on Godot
// types, so they're only exposed to C++ translation units.
extern "C" void host_set_current_object(godot::Node2D *node);
extern "C" godot::Node2D *host_get_current_object(void);
#endif

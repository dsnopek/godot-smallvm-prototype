// embed_main.cpp - Example C++ host that embeds the MicroBlocks VM.
//
// This program shows how to compile the MicroBlocks interpreter into your own
// C++ project (no static/dynamic library — just point the build at the .c
// files in ../vm/) and how to expose host-specific blocks via a custom
// primitive set.
//
// At runtime the program:
//   1. Initializes the VM (memory, primitive sets, persistent code store).
//   2. Registers a custom "host" primitive set (see custom_prims.c).
//   3. Optionally calls those primitives directly from C++ for a smoke test.
//   4. Opens a pseudo-terminal (/dev/pts/<n>) and runs vmLoop, so the
//      MicroBlocks IDE can connect, send compiled chunks, and call into
//      the host primitives from scripts.

extern "C" {
#include <stdint.h>
#include "mem.h"
#include "interp.h"
#include "persist.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

extern "C" {
    // Defined in custom_prims.c.
    void addCustomHostPrims(void);
    void host_register_object(int id, const char *name, int x, int y);
    void host_set_current_object(int id);
    int  host_object_x(int id);
    int  host_object_y(int id);
    // Defined in embed_platform.c.
    int  embed_open_pty(void);
    const char *embed_pty_name(void);
    void embed_close_pty(void);
    void embed_set_code_file(const char *path);
}

static void smoke_test_custom_prims() {
    // Demonstrate calling our custom primitives directly from C++. This
    // proves the primitives are wired up before we hand control to the IDE.
    // We invoke them through doPrimitiveCall, the same path the interpreter
    // uses when it executes a compiled primitive call instruction.

    printf("[host] smoke-testing custom primitives via doPrimitiveCall...\n");

    // [host:greet "World"] -> prints a greeting and returns the int 0.
    // newStringFromBytes is declared in mem.h; it allocates a String OBJ.
    OBJ greetArgs[1];
    greetArgs[0] = newStringFromBytes("World", 5);
    OBJ greetResult = doPrimitiveCall(BLEPrims, "greet", 1, greetArgs);
    printf("[host]   greet returned: %d (expected 0)\n", obj2int(greetResult));

    // [host:add 3 4] -> 7
    OBJ addArgs[2] = { int2obj(3), int2obj(4) };
    OBJ addResult = doPrimitiveCall(BLEPrims, "add", 2, addArgs);
    printf("[host]   add(3, 4) returned: %d (expected 7)\n", obj2int(addResult));

    // [host:square 5] -> 25
    OBJ squareArgs[1] = { int2obj(5) };
    OBJ squareResult = doPrimitiveCall(BLEPrims, "square", 1, squareArgs);
    printf("[host]   square(5) returned: %d (expected 25)\n",
        obj2int(squareResult));

    // Implicit current-object pattern. The host registers two objects and
    // designates one as "current"; a script primitive that takes no handle
    // still operates on the right object because it reads the C global.
    host_register_object(0, "player", 100, 200);
    host_register_object(1, "enemy",  300, 400);
    host_set_current_object(0);
    OBJ moveArgs[2] = { int2obj(5), int2obj(-3) };
    doPrimitiveCall(BLEPrims, "moveBy", 2, moveArgs);
    printf("[host]   player after moveBy(5,-3): x=%d y=%d (expected 105 197)\n",
        host_object_x(0), host_object_y(0));

    // Switch the current object and verify the same primitive now affects
    // the other host object — proving the implicit context follows the
    // host's host_set_current_object() call.
    host_set_current_object(1);
    OBJ moveArgs2[2] = { int2obj(-50), int2obj(0) };
    doPrimitiveCall(BLEPrims, "moveBy", 2, moveArgs2);
    printf("[host]   enemy  after moveBy(-50,0): x=%d y=%d (expected 250 400)\n",
        host_object_x(1), host_object_y(1));
}

static void on_signal(int sig) {
    (void) sig;
    embed_close_pty();
    _exit(0);
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [--smoke-test] [--no-ide] [code-file]\n"
        "\n"
        "  code-file    Path to the persistent bytecode store. Created if\n"
        "               missing; updated as the IDE saves scripts.\n"
        "               Defaults to ./ublockscode.bin\n"
        "  --no-ide     Don't open a PTY. Just restore + run the scripts\n"
        "               already in code-file, then loop.\n"
        "  --smoke-test Call custom primitives directly and exit (CI use).\n",
        prog);
}

int main(int argc, char *argv[]) {
    bool runVmLoop = true;
    bool openIde = true;
    const char *codeFile = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--smoke-test") == 0) {
            runVmLoop = false;
        } else if (strcmp(argv[i], "--no-ide") == 0) {
            openIde = false;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown flag: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        } else {
            codeFile = argv[i];
        }
    }
    if (codeFile) embed_set_code_file(codeFile);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    atexit(embed_close_pty);

    // 1. Bring the VM up.
    memInit();                  // object memory + GC
    primsInit();                // registers vars/data/misc; the other set
                                // entries are filled by empty stubs in
                                // embed_platform.c.

    // 2. Add our host-specific primitive set. This *replaces* the stub
    //    registration that primsInit() did for BLEPrims (slot 9) so that
    //    the IDE-visible name "ble" now refers to our host primitives.
    //    See custom_prims.c for the rationale and for how to add your own
    //    blocks.
    addCustomHostPrims();

    // 3. Restore any previously stored scripts from disk (code file lives
    //    next to the binary, just like the Linux build's "ublockscode").
    restoreScripts();

    if (!runVmLoop) {
        smoke_test_custom_prims();
        return 0;
    }

    // 4. Optionally open a pseudo-terminal so the MicroBlocks IDE can connect.
    //    With --no-ide we skip this entirely: scripts already persisted to
    //    the code-file are simply restored and run, no IDE needed.
    if (openIde) {
        if (embed_open_pty() < 0) {
            fprintf(stderr, "Failed to open pseudo terminal\n");
            return 1;
        }
        printf("MicroBlocks VM embedded host — connect IDE on: %s\n",
            embed_pty_name());
        fflush(stdout);
    } else {
        printf("MicroBlocks VM embedded host — running scripts from %s "
               "(no IDE)\n",
            codeFile ? codeFile : "ublockscode.bin");
        fflush(stdout);
    }

    // Auto-start any "when started" / "when condition" hat tasks from the
    // restored code store.
    startAll();

    // 5. Hand control to the interpreter. vmLoop never returns.
    vmLoop();
    return 0;
}

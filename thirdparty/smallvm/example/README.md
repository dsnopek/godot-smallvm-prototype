# Embedding the MicroBlocks VM in a C++ host

This directory holds a standalone example host that demonstrates how to
embed the MicroBlocks VM into another C++ program, register custom
"blocks" that call native C functions, and let the MicroBlocks IDE
deliver scripts over a pseudo-terminal.

The example links against the static library produced from the VM
sources in `../src/`. The same `libsmallvm.a` is what the parent
GDExtension project will consume.

## Layout

| File | Purpose |
| --- | --- |
| `embed_main.cpp` | C++ host with `main()`. Brings the VM up and either runs the smoke test or hands off to `vmLoop`. |
| `embed_platform.c` | Platform glue: timing, persistent code file, PTY-based IDE transport, plus stubs for every hardware function the VM forward-declares but we don't need. |
| `custom_prims.c` | The application-specific primitive set. This is the only file you typically edit to add new blocks. |
| `SConscript` | Compiles the three files above and links them with `libsmallvm.a` from `../SConscript` to produce `./vm_embed`. |
| `test_embed.sh` | Runs two automated checks (see below). |

## Build prerequisites

The VM's object model is 32-bit only — `mem.c::memInit()` panics on
systems where `sizeof(int*) != 4`. So the binary has to be built as 32-bit.
On 64-bit Linux that means installing multilib:

```sh
sudo apt install gcc-multilib g++-multilib
```

## Building & running

The build is driven from the parent project's top-level SCons setup:

```sh
cd ../../..              # godot-smallvm-prototype root
scons example            # builds libsmallvm.a, then ./vm_embed
./thirdparty/smallvm/example/vm_embed
```

`vm_embed` prints a `/dev/pts/N` path on startup — point the MicroBlocks
IDE at it (Connect → Open serial port) to develop scripts that call your
custom blocks.

## Testing

```sh
./test_embed.sh
```

- **Phase 1** runs `./vm_embed --smoke-test`, which calls each custom
  primitive directly through `doPrimitiveCall(...)`. This proves your
  primitive set is registered and dispatch works without needing an IDE.
- **Phase 2** launches the VM normally, opens the printed PTY itself, and
  speaks the MicroBlocks serial protocol to it (sends a `getVersionMsg`,
  reads back the `versionMsg` reply). This proves the recv/send transport
  works and that the IDE protocol is alive end-to-end.

## Adding your own blocks

Adding a block has two halves: the **C side** (what runs when the block
fires) and the **IDE side** (how the block appears in the palette).

### C side

Edit `custom_prims.c`. Each block is a C function with signature
`OBJ myPrim(int argCount, OBJ *args)`. Append a `PrimEntry` row in the
`entries[]` table and you're done — no other file needs to change.

### IDE side — making the blocks show up in the palette

The MicroBlocks compiler treats any block whose op-name has the form
`[primSetName:primName]` as a primitive call (see the comment in
`custom_prims.c` for why we use the `ble` slot here). To make our three
primitives appear as blocks the user can drag onto a script, we ship a
**library file** alongside the example: `HostBlocks.ubl`.

To load it in the running IDE:

1. Start the embedded VM: `./vm_embed`. It prints `/dev/pts/N`.
2. In the MicroBlocks IDE, connect to that PTY (Connect → Open
   serial port → paste the PTY path).
3. Open the **File menu → "Add Library"** (the "+" icon next to the
   palette also works), pick `embed_example/HostBlocks.ubl`, and the
   new "Host Blocks" category appears in the palette with three blocks:
   `greet _`, `add _ + _`, `square of _`.

The `.ubl` file is plain text — open it to see the format. The mapping is
direct: each `spec` line names a `[ble:xxx]` primitive that must exist
in `custom_prims.c`'s `entries[]`.

### Picking a slot

The example uses the `BLEPrims` slot (index 9) so we don't have to touch
any source under `vm/`. If you'd rather have a friendlier name than
`ble` showing up in compiled bytecode and `.ubl` files, you have two
clean options:

1. **Rename the slot, IDE-side only.** Change the corresponding entry in
   `ide/MicroBlocksCompiler.gp::initPrimsets` (currently `"ble"`) to
   `"host"` or whatever you prefer, then rebuild the IDE with
   `./build.sh`. The C VM never sees the name, just the slot index, so
   no VM changes needed. After this, your `.ubl` would use
   `[host:greet]` instead of `[ble:greet]`.
2. **Add a new slot.** Append `HostPrims` to the `PrimitiveSetIndex` enum
   in `vm/interp.h` (just before `PrimitiveSetCount`), append the matching
   name to the IDE's `primSetNames` list, and pass `HostPrims` instead of
   `BLEPrims` to `addPrimitiveSet`. This is the officially supported
   extension point — see the comment above the enum.

## Persistence — running a "saved project" without the IDE

A short, important distinction first:

- **`.ubp`** is a *source* file. It contains the GP text the IDE uses to
  redraw the blocks (`module`, `spec`, `to`, scripts, …). To go from
  `.ubp` to running code the compiler in `ide/MicroBlocksCompiler.gp`
  has to turn each script into a bytecode chunk. That compiler is
  written in GP and only runs inside the IDE — there is no C
  implementation of it.
- **The persistent code store** is a *binary* file containing the
  already-compiled chunks. The VM consumes this directly via
  `restoreScripts()` at boot. **This is the thing you actually want to
  ship.**

The good news: every time the IDE sends a chunk to `vm_embed` over the
PTY, our `writeCodeFile()` appends it to the code store. So once you've
developed a project against the IDE, the bytecode is *already* sitting
on disk — `.ubp` save/load is just for the IDE's own future editing.

### Recommended workflow

```sh
# 1. Develop with the IDE attached.
./vm_embed test.ubcode          # uses ./test.ubcode as the code store
# In the IDE:
#   - connect to the printed /dev/pts/N
#   - File → Add Library → embed_example/HostBlocks.ubl
#   - drag blocks, hit play, iterate
#   - File → Save Project As → test.ubp   (this is OPTIONAL — only if you
#                                          want to re-edit blocks later)

# 2. Quit the IDE. Quit vm_embed (Ctrl-C). The bytecode is now baked
#    into ./test.ubcode.

# 3. Ship vm_embed + test.ubcode together. To run with no IDE:
./vm_embed --no-ide test.ubcode
# restoreScripts() loads test.ubcode, startAll() fires the "when started"
# hats. The PTY is never opened.
```

Both `test.ubp` and `test.ubcode` describe the same project; the `.ubp`
is the human-editable copy, the `.ubcode` is the runnable copy. Keep the
`.ubp` in source control if you want to iterate on the blocks; keep the
`.ubcode` next to the binary you distribute.

### CLI summary

```
vm_embed [--smoke-test] [--no-ide] [code-file]
```

| Flag / arg     | Effect |
| --- | --- |
| `code-file`    | Persistent bytecode store. Default `./ublockscode.bin`. |
| `--no-ide`     | Skip the PTY; just load + run the code file. |
| `--smoke-test` | Call custom prims via `doPrimitiveCall` and exit. |

### Why not just parse `.ubp` directly in C?

You can, but only as far as separating the modules/specs/`to`/script
declarations — turning a script tree into the 32-bit opcodes the VM
runs requires `instructionsFor`, `bytesForInstructions`, the literal
table builder, function-name resolution, all the work in
`MicroBlocksCompiler.gp`. Reproducing it in C is a significant project
(thousands of lines of GP, plus the GP language semantics those scripts
assume). If you really need a host-side `.ubp` → `.ucode` step, the
practical route is to drive the existing IDE compiler from a small GP
script (`gp/gp-linux64bit … runtime/lib/* loadIDE.gp <yourScript>`), not
to rewrite it.

# godot-smallvm-prototype

A Godot GDExtension that embeds the [MicroBlocks](https://bitbucket.org/john_maloney/smallvm/src/dev/)
("SmallVM") interpreter so that Godot scenes can run scripts authored
visually in the MicroBlocks IDE.

## Layout

- `thirdparty/smallvm/src/` — vendored MicroBlocks VM C sources, built
  into `libsmallvm.a` (32-bit) by `thirdparty/smallvm/SConscript`.
- `thirdparty/smallvm/example/` — standalone host program (`vm_embed`)
  that exposes the same VM over a pseudo-terminal. Used as the scratchpad
  where the MicroBlocks IDE connects to author scripts.
- `src/classes/small_vm.{h,cpp}` — the `SmallVM` `RefCounted` class
  surfaced to GDScript. One entry point of interest:
  `execute_bytecode(bytecode: PackedByteArray, current_object: Node2D)`.
- `src/platform/small_vm_platform.c` — VM platform stubs for the
  GDExtension build: timing, in-memory bytecode buffer (no file or PTY),
  and no-op stubs for the hardware functions the VM forward-declares.
- `src/platform/small_vm_custom_prims.cpp` — the custom primitive set.
  Holds a `Node2D *` as the "current object" and exposes `set x to`,
  `set y to`, `move by`, `x`, `y`, and `greet` blocks that read/write
  the Node2D's `position`.

The VM keeps its entire state in static globals, so there is one VM per
process; every `SmallVM.new()` shares it.

## Building

`scons` builds both the GDExtension (`bin/<platform>/libsmallvm.*`) and
the standalone `vm_embed` host. The whole project must be 32-bit because
the VM's `OBJ` type is a tagged 32-bit pointer (`mem.c::memInit()` panics
if `sizeof(int*) != 4`). The SConstruct defaults `arch=x86_32` and
rejects any other arch. On 64-bit Linux you'll need multilib:

```sh
sudo apt install gcc-multilib g++-multilib
```

## Authoring scripts in the MicroBlocks IDE

The IDE is the only practical way to produce SmallVM bytecode — its
compiler is written in GP and doesn't have a C port. The workflow is to
edit against the standalone host, then ship the persisted bytecode.

1. Run the standalone host pointed at the code file you want to fill:
   ```sh
   ./thirdparty/smallvm/example/vm_embed project/my_script.ubcode
   ```
   It prints a `/dev/pts/N` path on startup.
2. In the MicroBlocks IDE, **Connect → Open serial port** and paste that
   path.
3. **File → Add Library → `thirdparty/smallvm/example/HostBlocks.ubl`**
   to import the host blocks. The GDExtension implements every block in
   that library — `set x to`, `set y to`, `move by`, `x`, `y` against the
   current Node2D, plus `greet` / `add` / `square` as host-side helpers.
4. Drag blocks under a `when started` hat and press play to iterate.
   Every chunk the IDE sends is appended to `my_script.ubcode` on disk.
5. (Optional) **File → Save Project As → `my_script.ubp`** for a
   re-editable source copy. Only `my_script.ubcode` is needed at runtime.

## Running bytecode against a Node2D

Load the `.ubcode` bytes and hand them to `SmallVM.execute_bytecode`
together with the Node2D the script should drive:

```gdscript
extends Node2D

@onready var sprite: Sprite2D = %Sprite2D

var smallvm := SmallVM.new()
var bytecode: PackedByteArray = FileAccess.get_file_as_bytes("res://my_script.ubcode")

func _ready() -> void:
    smallvm.execute_bytecode(bytecode, sprite)
```

`execute_bytecode` loads the bytecode, designates the Node2D as the
current object, fires any `when started` hat tasks, and runs every task
to completion before returning. The call is **synchronous** — design
scripts to terminate (no `forever` loops without a `stop` block) or
they'll block the calling thread.

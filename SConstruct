#!/usr/bin/env python
import os
import sys

from methods import print_error


libname = "smallvm"
projectdir = "project"

localEnv = Environment(tools=["default"], PLATFORM="")

# Build profiles can be used to decrease compile times.
# You can either specify "disabled_classes", OR
# explicitly specify "enabled_classes" which disables all other classes.
# Modify the example file as needed and uncomment the line below or
# manually specify the build_profile parameter when running SCons.

# localEnv["build_profile"] = "build_profile.json"

customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()

# The MicroBlocks VM (thirdparty/smallvm) is 32-bit only: its OBJ type is a
# tagged int* and mem.c::memInit() panics if sizeof(int*) != 4. We default
# the whole project to x86_32 so the GDExtension shared library, the static
# VM library, and the example host program all share one toolchain.
# Setting env["arch"] BEFORE godot-cpp's SConstruct runs makes this the
# default; the user can still pass arch=... on the command line (in which
# case the validation below will reject anything other than x86_32).
if "arch" not in ARGUMENTS:
    env["arch"] = "x86_32"

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    sys.exit(1)

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

# Post-config arch validation. By this point env["arch"] has been processed
# by godot-cpp (which may have applied user overrides, host detection,
# etc.). The VM only works as 32-bit, so anything else is a hard error.
if env["arch"] != "x86_32":
    print_error(
        "This project must be built for 32-bit x86 (arch=x86_32). The "
        "embedded MicroBlocks VM in thirdparty/smallvm/ is 32-bit only; "
        "mem.c::memInit() will panic at runtime if sizeof(int*) != 4. "
        "Got arch={}.".format(env["arch"])
    )
    print_error(
        "On 64-bit Linux you also need the multilib toolchain: "
        "`sudo apt install gcc-multilib g++-multilib`."
    )
    sys.exit(1)

# Build the embedded MicroBlocks VM as a static library. We share the
# godot-cpp env so the library is compiled with the exact same -m32 /
# -march=i686 / -fPIC flags as the GDExtension shared library that will
# link it. The SConscript Clone()s the env locally before adding warning
# suppressions, so its overrides don't leak back here.
smallvm_lib, smallvm_cppflags = SConscript(
    "thirdparty/smallvm/SConscript",
    exports={"env": env},
)

# Build the standalone example application that exercises the static library.
# This is also a default target (see Default() in example/SConscript).
SConscript(
    "thirdparty/smallvm/example/SConscript",
    exports={
        "env": env,
        "smallvm_lib": smallvm_lib,
        "smallvm_cppflags": smallvm_cppflags,
    },
)

# The GDExtension shared library wants to call into the VM, so expose the
# VM's headers and defines (CPPPATH / CPPDEFINES from smallvm_cppflags).
# Compile/link flags (-m32, -march=i686) are already in env from godot-cpp.
env.Append(CPPPATH=["src/"] + smallvm_cppflags["CPPPATH"])
env.Append(CPPDEFINES=smallvm_cppflags["CPPDEFINES"])
sources = Glob("src/*.cpp") + Glob("src/classes/*.cpp") + Glob("src/platform/*.cpp")
# Also pick up C platform glue (small_vm_platform.c) — Glob("*.cpp") misses it.
sources += Glob("src/*.c") + Glob("src/classes/*.c") + Glob("src/platform/*.c")

# Link libsmallvm.a into the shared library. SCons forbids passing a
# StaticLibrary node directly in `source=` for a SharedLibrary target
# ("static is not compatible with shared"), so we go through LIBS / LIBPATH
# and add an explicit Depends() to keep the lib rebuilt when its sources
# change.
env.Append(LIBPATH=[smallvm_lib[0].dir])
env.Append(LIBS=["smallvm"])

if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

# .dev doesn't inhibit compatibility, so we don't need to key it.
# .universal just means "compatible with all relevant arches" so we don't need to key it.
suffix = env['suffix'].replace(".dev", "").replace(".universal", "")

lib_filename = "{}{}{}{}".format(env.subst('$SHLIBPREFIX'), libname, suffix, env.subst('$SHLIBSUFFIX'))

library = env.SharedLibrary(
    "bin/{}/{}".format(env['platform'], lib_filename),
    source=sources,
)
env.Depends(library, smallvm_lib)

copy = env.Install("{}/bin/{}/".format(projectdir, env["platform"]), library)

default_args = [library, copy]
Default(*default_args)

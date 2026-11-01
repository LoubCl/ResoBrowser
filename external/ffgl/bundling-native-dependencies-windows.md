# Bundling native dependencies on Windows

If your plugin ships a DLL of its own — ONNX Runtime, OpenCV, ffmpeg, anything —
read this. On Windows there is only one slot per DLL *file name* in a process, and
without a little care your plugin will end up using somebody else's copy of your
dependency.

## What goes wrong

Windows resolves a DLL import by its bare file name, and it checks the modules
already loaded into the process **before** it searches any directory. So if the
host, or another plugin loaded earlier, already has a DLL with the same file name
in memory, your plugin gets *that* one — no matter which folder yours sits in.

Loading your copy yourself does not get you out of it either: the already-loaded
check applies to `LoadLibrary` too, even when you pass a full path.

This is not specific to Resolume. Two plugins that each bundle their own build of
the same library collide with each other in exactly the same way, in any host.

## Why you may have noticed it recently

Resolume 7.28 is the first version to use ONNX Runtime for its own AI effects, so
from 7.28 on, `onnxruntime.dll` and `DirectML.dll` are occupied for the lifetime
of the process. A plugin that bundles ONNX Runtime and worked fine on 7.27 can
therefore fail on 7.28 — before 7.28, nothing in the process was using those
names, and your copy was found.

## What the failure looks like (ONNX Runtime)

ONNX Runtime is backward compatible but not forward compatible.
`OrtGetApiBase()->GetApi(ORT_API_VERSION)` hands back a valid pointer when the
runtime that got loaded is the same version or newer than the headers you built
against, and **`nullptr`** when it is older. `ORT_API_VERSION` tracks the minor
version, so headers from 1.24 ask for 24.

`Ort::GetApi()` dereferences that pointer without checking it, so if you built
against 1.24 and the host's 1.21 is what you got, your first ONNX call is an
access violation reading a very low address — the offset of the function you were
about to call inside a null `OrtApi`. The crash lands in your plugin, which is
why users report it to you.

Worth doing regardless of everything below: check that pointer. Define
`ORT_API_MANUAL_INIT`, call `OrtGetApiBase()->GetApi(ORT_API_VERSION)` yourself,
and if it comes back null, report which runtime you actually got
(`OrtGetApiBase()->GetVersionString()` works even then) and disable your effect
instead of faulting.

## What if I do nothing

It may well work. If nothing else in the process happens to use your
dependency's file name, your copy is the one that gets found, and nothing is
wrong until the day that changes — a host version that picks up the same library
for its own features, a user who installs another plugin that bundles it, or
simply a different load order on the next launch. This is not a failure you can
count on seeing during development, and when it does turn up it turns up on
someone else's machine, as a crash inside your plugin.

Leaving the DLL next to your plugin is also a bet on the host searching that
directory at all. A host is free to restrict or switch off the directory search
for the modules it loads — `SetDefaultDllDirectories` and the
`LOAD_LIBRARY_SEARCH_*` flags exist for that — and it is a reasonable thing for a
host to do, since it stops a plugin from quietly substituting a DLL inside the
host process. The Adobe suite has worked this way for decades. Manifest
redirection does not go through the directory search, so a dependency declared as
a private assembly is not exposed to this; one that is merely sitting in a folder
is.

## The fix: ship your dependency in a private side-by-side assembly

Manifest redirection is consulted *before* the loaded-module list, so a
dependency declared as a private assembly is resolved to your copy even when a
DLL of the same name is already loaded. This is verified working: an ONNX Runtime
1.24 plugin loading its own runtime inside Resolume Arena 7.28, which has 1.21
loaded at the same time.

### 1. Lay the files out

Put the dependency in a subfolder next to your plugin, named after the assembly:

```
MyPlugin.dll
MyPlugin.onnx
MyPlugin.Ort\
    MyPlugin.Ort.manifest
    onnxruntime.dll
    onnxruntime_providers_shared.dll
    DirectML.dll
```

There is a second manifest, `MyPlugin.manifest` in step 3, that does not appear
here: it is a build input, embedded into `MyPlugin.dll` in step 4, and is not
shipped as a file.

The DLLs have to sit beside the assembly manifest — that is where the loader
looks for the files an assembly declares. Anything your plugin loads by path of
its own (a model file, say) stays next to the plugin as before.

### 2. Write the assembly manifest

`MyPlugin.Ort\MyPlugin.Ort.manifest`:

```xml
<?xml version="1.0" encoding="utf-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32"
                    name="MyPlugin.Ort"
                    version="1.24.0.0"
                    processorArchitecture="amd64"/>
  <file name="onnxruntime.dll"/>
  <file name="onnxruntime_providers_shared.dll"/>
  <file name="DirectML.dll"/>
</assembly>
```

**Name the assembly after your plugin.** The identity is what keeps you separate
from everyone else, so if two plugins both call theirs `Ort`, they are back to
fighting over one slot. Use something nobody else will pick.

### 3. Declare the dependency in your plugin

`MyPlugin.manifest`:

```xml
<?xml version="1.0" encoding="utf-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32"
                    name="MyPlugin"
                    version="1.0.0.0"
                    processorArchitecture="amd64"/>
  <dependency>
    <dependentAssembly>
      <assemblyIdentity type="win32"
                        name="MyPlugin.Ort"
                        version="1.24.0.0"
                        processorArchitecture="amd64"/>
    </dependentAssembly>
  </dependency>
</assembly>
```

### 4. Embed it in the plugin DLL

In Visual Studio, under Linker > Manifest File, set *Additional Manifest Files*
to `MyPlugin.manifest` and *Generate Manifest* / *Embed Manifest* to Yes — or on
the command line, `/MANIFEST:EMBED /MANIFESTINPUT:MyPlugin.manifest`. The linker
embeds it at resource id **2** for a DLL, which is the id the loader reads.

To add one to a DLL you have already built, `mt.exe` from the Windows SDK does it
without rebuilding:

```
mt.exe -manifest MyPlugin.manifest -outputresource:MyPlugin.dll;#2
mt.exe -inputresource:MyPlugin.dll;#2 -out:check.manifest
```

Note this invalidates an Authenticode signature, so sign afterwards. In
PowerShell, quote the argument — `#` starts a comment there.

### 5. Check that it worked

Load your effect and look at the host process in Process Explorer, DLL pane. You
should see **two** copies of your dependency loaded, from different folders and
with different versions: the host's, and yours out of your assembly folder. If
you only see the host's, the redirection is not taking effect.

When it does not work, `sxstrace` tells you why — it needs an elevated prompt:

```
sxstrace Trace -logfile:sxs.etl
   (start the host, load your effect, then Ctrl+C)
sxstrace Parse -logfile:sxs.etl -outfile:sxs.txt
```

`sxs.txt` lists every path the loader probed and the exact reason it gave up. A
hard activation failure shows up as "The application has failed to start because
its side-by-side configuration is incorrect."

## Two things to keep in mind

**DirectML.** ONNX Runtime delay-loads `DirectML.dll` by bare name the first time
it needs it, which is generally too late for the activation context to still be
in force, so you may get the host's DirectML even with everything above in place.
In practice a difference in minor version is harmless — DirectML negotiates
feature levels — but if your effect loads and then misbehaves rather than
crashing, look there first.

**Provider DLLs must match your core.** `onnxruntime_providers_shared.dll` and
any execution-provider DLLs have to be the same version as your
`onnxruntime.dll`. Put them in the assembly alongside it, not next to your
plugin.

## macOS

None of this applies. Mach-O records the path of a dependency rather than a bare
name, so a dylib you ship next to your plugin and reference through
`@loader_path` is already yours alone. This is a Windows problem only.

## Alternative: give your copy a unique file name

The collision is over the file name, so renaming your dependency —
`myplugin-ort.dll` rather than `onnxruntime.dll` — sidesteps it on the same
principle, and is less work than the manifest if your build makes it easy to
change what your import table asks for. We have only verified the manifest route
above, so treat this as the untested option of the two.

Note that renaming only settles the collision over the name. The import is still
resolved through the directory search, so unlike the manifest it stays dependent
on the host searching the folder your plugin was loaded from.

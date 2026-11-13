# ngspice backend

Desktop builds load libngspice at runtime; no ngspice headers or import libraries are required. Search order is `DE_NGSPICE_LIB`, a DLL beside the executable, then the system library name (`ngspice.dll`, `libngspice.so.0`, or `libngspice.dylib`).

Examples:

```text
designElectronics --spice-run circuit.dez --tran 1m 50m --out result.csv
designElectronics --spice-run circuit.dez --ac 10 1Meg 10 --source battery
designElectronics --spice-run circuit.dez --dc battery 0 5 0.1
```

SI suffixes `p`, `n`, `u`, `m`, `k`, `M`, and `G` are accepted. CSV output starts with `scale`, followed by ngspice vector columns. If no shared library is installed, self-test prints `SPICE: SKIP`; CLI exits non-zero and explains how to set `DE_NGSPICE_LIB`.

The backend currently uses synchronous `ngSpice_Command("run")`, serialised by a process mutex. It registers the official `sharedspice.h` callbacks and reads vectors through `ngSpice_CurPlot`, `ngSpice_AllVecs`, and `ngGet_Vec_Info`; vendor builds with incompatible vector layouts are reported as a safe runtime failure. Digital outputs and programmed Arduino behaviour are not modelled by this backend. `externalDigital=true` is explicitly rejected rather than silently driving every external voltage source at zero; numeric self-tests do not enable it.

For local verification, ngspice 47 was downloaded from <https://sourceforge.net/projects/ngspice/files/ng-spice-rework/47/ngspice-47_dll_64.7z/download> into an ignored `build-spice-c/ngspice` directory. Archive SHA-256: `5BF5EEC7DB989F74849CDB32F7DF62693F4F149CCEEEBCBD222EB35C2BB821EB`; extracted `ngspice.dll` SHA-256: `0AD7C4CE9794EFAEFED12552A077DE63FC021FE2C98C164831E8BB92D3D72D9E`. The DLL also needs the package's `libomp140.x86_64.dll`, `sndfile.dll`, `samplerate.dll`, and `spinit`/model files.

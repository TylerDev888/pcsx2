# PCSX2 with additional PINE socket support - Used for creating programs that need remote control of the pcsx2.

The PINE protocol had no way for external tools to control execution or inspect EE/IOP CPU state. This adds 30 new opcodes (0x10–0x2D) that expose the complete PCSX2 GUI debugger interface over the existing PINE socket, including named save states, disassembly, symbol database queries, and local variable inspection.

## New opcodes

### Execution control and breakpoints

| Value | Name | Description |
|-------|------|-------------|
| `0x10` | `MsgGetProgramCounter` | EE PC + paused flag (`u32` + `u8`) |
| `0x11` | `MsgPause` | Halt at next instruction boundary |
| `0x12` | `MsgResume` | Resume execution |
| `0x13` | `MsgStep` | Execute one instruction while paused; returns new PC |
| `0x14` | `MsgSetBreakpoint` | Register PC breakpoint at EE address |
| `0x15` | `MsgClearBreakpoint` | Remove a breakpoint; `IPC_FAIL` if not found |
| `0x16` | `MsgClearAllBreakpoints` | Remove all PC breakpoints |

### Register access

| Value | Name | Description |
|-------|------|-------------|
| `0x17` | `MsgGetRegisters` | Quick read: 32 EE GPRs (lower 32 bits) + PC + HI + LO |
| `0x18` | `MsgGetRegister` | Read any register on EE or IOP by CPU type, category, index → 16-byte u128 |
| `0x19` | `MsgSetRegister` | Write any register (paused only) by CPU type, category, index |

### Process state

| Value | Name | Description |
|-------|------|-------------|
| `0x1A` | `MsgGetEEThreads` | List all EE threads (TID, PC, status, wait, priority, entry, stack top) |
| `0x1B` | `MsgGetIOPThreads` | List all IOP threads (same format) |
| `0x1C` | `MsgGetModules` | List loaded IOP modules (name, version, text/data/bss addresses and sizes) |
| `0x1D` | `MsgGetStack` | Walk the EE call stack (entry, PC, SP, frame size per frame) |

### High-level stepping and symbol lookup

| Value | Name | Description |
|-------|------|-------------|
| `0x1E` | `MsgStepInto` | Set temp BP at next instruction and resume (non-blocking) |
| `0x1F` | `MsgStepOver` | Set temp BP past function calls and resume (non-blocking) |
| `0x20` | `MsgStepOut` | Walk stack to caller, set temp BP, and resume (non-blocking) |
| `0x21` | `MsgGetSymbol` | Look up EE function symbol name by address |

### Save-state and emulator control

| Value | Name | Description |
|-------|------|-------------|
| `0x22` | `MsgSaveStateFile` | Save state to a named file path (async; `u16` path length prefix + UTF-8 path) |
| `0x23` | `MsgLoadStateFile` | Load state from a named file path (blocking; `IPC_FAIL` on error) |
| `0x24` | `MsgReset` | Cold-boot reset |
| `0x25` | `MsgFrameAdvance` | Advance N video frames then pause (`u8` count) |
| `0x26` | `MsgGetFPS` | Current emulated framerate as `f32` |
| `0x27` | `MsgSetLimiterMode` | Set speed limiter (0=Nominal, 1=Turbo, 2=Slomo, 3=Unlimited) |

### Breakpoint inspection and disassembly

| Value | Name | Description |
|-------|------|-------------|
| `0x28` | `MsgListBreakpoints` | List active EE and/or IOP PC breakpoints (addr + enabled + cpu per entry) |
| `0x29` | `MsgDisassemble` | Disassemble up to 1000 instructions at an address on EE or IOP |

### Symbol database queries

| Value | Name | Description |
|-------|------|-------------|
| `0x2A` | `MsgListFunctions` | Paginated list of EE function symbols (offset + max_count → total + [addr, size, name]) |
| `0x2B` | `MsgGetSymbolByName` | Look up any EE symbol by demangled name → address + size |
| `0x2C` | `MsgListGlobals` | Paginated list of EE global variables (same format as `MsgListFunctions`) |
| `0x2D` | `MsgGetLocals` | List parameters and locals for the function containing a given EE address |

### Memory Watches
| Opcode | Name | Args |
|--------|------|------|
| `0x2E` | `MsgAddWatch` | `cpu(u8)`, `start(u32)`, `end(u32, exclusive)`, `cond(u8)` |
| `0x2F` | `MsgRemoveWatch` | `cpu(u8)`, `start(u32)`, `end(u32)` |
| `0x30` | `MsgListWatches` | `cpu_sel(u8)` — `0`=EE, `1`=IOP, `0xFF`=both |
| `0x31` | `MsgClearAllWatches` | *(none)* |

`cond` bitmask: `0x01`=read, `0x02`=write, `0x03`=read+write. Watchpoints always use `MEMCHECK_BREAK` (pause on hit).



![Windows Build Status](https://img.shields.io/github/actions/workflow/status/PCSX2/pcsx2/windows_build_matrix.yml?label=%F0%9F%96%A5%EF%B8%8F%20Windows%20Builds)
![Linux Build Status](https://img.shields.io/github/actions/workflow/status/PCSX2/pcsx2/linux_build_matrix.yml?label=%F0%9F%90%A7%20Linux%20Builds)
![MacOS Build Status](https://img.shields.io/github/actions/workflow/status/PCSX2/pcsx2/macos_build_matrix.yml?label=%F0%9F%8D%8E%20MacOS%20Builds)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/1f7c0d75fec74d6daa6adb084e5b4f71)](https://app.codacy.com/gh/PCSX2/pcsx2/dashboard?utm_source=github.com&utm_medium=referral&utm_content=PCSX2/pcsx2&utm_campaign=Badge_Grade)
[![Discord Server](https://img.shields.io/discord/309643527816609793?color=%235CA8FA&label=PCSX2%20Discord&logo=discord&logoColor=white)](https://discord.com/invite/TCz3t9k)

PCSX2 is a free and open-source PlayStation 2 (PS2) emulator. Its purpose is to emulate the PS2's hardware, using a combination of MIPS CPU [Interpreters](<https://en.wikipedia.org/wiki/Interpreter_(computing)>), [Recompilers](https://en.wikipedia.org/wiki/Dynamic_recompilation) and a [Virtual Machine](https://en.wikipedia.org/wiki/Virtual_machine) which manages hardware states and PS2 system memory. This allows you to play PS2 games on your PC, with many additional features and benefits.

## Project Details

PCSX2 has been in development for more than 20 years. Past versions could only run a few public domain game demos, but newer versions can run most games at full speed, including popular titles such as Final Fantasy X and Devil May Cry 3. Visit the [PCSX2 compatibility list](https://pcsx2.net/compat/) to check the latest compatibility status of games (with more than 2500 titles tested).

Installers and binaries for both stable and nightly builds are available from [our website](https://pcsx2.net/downloads/).

## System Requirements

PCSX2 supports Windows, Linux, and Mac platforms. Our [setup documentation page](https://pcsx2.net/docs/setup/requirements) contains additional details on software and hardware requirements.

Please note that a BIOS dump from a legitimately-owned PS2 console is required to use the emulator. For more information, visit [this page](https://pcsx2.net/docs/setup/bios/).

## Contributing / Building

PCSX2 supports translation into other languages using [Crowdin](https://crowdin.com/project/pcsx2-emulator).

See the [Contribution Guide](https://pcsx2.net/docs/contributing/) for more info on how to contribute.

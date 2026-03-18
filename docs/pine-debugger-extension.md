# PINE Protocol Extension — Debugger Commands

## Summary

This proposal extends the PINE IPC protocol with opcodes 0x10–0x21 that expose full
EE and IOP CPU debugger state to external tools. The additions allow a remote client
to pause and resume emulation, single-step or high-level step (into/over/out) the EE
CPU, manage PC breakpoints, inspect all registers across all categories on both CPUs,
enumerate running threads and loaded IOP modules, walk the call stack, and look up
function symbols — all through the existing PINE socket without any separate
out-of-band channel.

All new opcodes are numbered above 0x0F, so no existing opcode values change and
clients that do not use the new opcodes are completely unaffected.

---

## Motivation

The existing PINE protocol covers memory read/write and basic emulator metadata.
External tools that need a full debugger experience — IDE integrations, breakpoint
managers, code coverage harnesses, automated test runners — currently have no way to
control execution or inspect CPU state without a separate, custom socket. Adding these
opcodes makes PINE a self-contained debugger transport.

---

## New Opcodes

### Execution control and breakpoints (0x10–0x16)

| Value | Name                     | Purpose                                              |
|-------|--------------------------|------------------------------------------------------|
| 0x10  | `MsgGetProgramCounter`   | Read the current EE program counter and paused flag  |
| 0x11  | `MsgPause`               | Halt EE execution at the next instruction boundary   |
| 0x12  | `MsgResume`              | Resume EE execution                                  |
| 0x13  | `MsgStep`                | Execute exactly one EE instruction while paused      |
| 0x14  | `MsgSetBreakpoint`       | Register a PC-execution breakpoint at an EE address  |
| 0x15  | `MsgClearBreakpoint`     | Remove the breakpoint at a given EE address          |
| 0x16  | `MsgClearAllBreakpoints` | Remove every PC breakpoint                           |

### Register access (0x17–0x19)

| Value | Name              | Purpose                                                     |
|-------|-------------------|-------------------------------------------------------------|
| 0x17  | `MsgGetRegisters` | Quick read: all 32 EE GPRs (lower 32 bits), PC, HI, LO     |
| 0x18  | `MsgGetRegister`  | Read one register by CPU type, category, and index → u128   |
| 0x19  | `MsgSetRegister`  | Write one register by CPU type, category, and index         |

### Process state (0x1A–0x1D)

| Value | Name               | Purpose                                |
|-------|--------------------|----------------------------------------|
| 0x1A  | `MsgGetEEThreads`  | List all EE (R5900) threads            |
| 0x1B  | `MsgGetIOPThreads` | List all IOP (R3000) threads           |
| 0x1C  | `MsgGetModules`    | List all loaded IOP modules            |
| 0x1D  | `MsgGetStack`      | Walk the EE call stack                 |

### High-level stepping and symbol lookup (0x1E–0x21)

| Value | Name            | Purpose                                              |
|-------|-----------------|------------------------------------------------------|
| 0x1E  | `MsgStepInto`   | Set temp breakpoint at next instruction and resume   |
| 0x1F  | `MsgStepOver`   | Set temp breakpoint past function calls and resume   |
| 0x20  | `MsgStepOut`    | Walk stack, set temp breakpoint at caller, resume    |
| 0x21  | `MsgGetSymbol`  | Look up EE function symbol name by address           |

---

## Packet Formats

All packets follow the existing PINE framing convention:

```
Request:  [total_size : u32 LE] [opcode : u8] [args…]
Reply:    [total_size : u32 LE] [status : u8] [data…]
```

`total_size` includes the four-byte size field itself. `status` is `IPC_OK (0x00)` on
success or `IPC_FAIL (0xFF)` on failure.

### MsgGetProgramCounter (0x10)

- **Request:** 5 bytes total — opcode only, no arguments.
- **Reply (OK):** 10 bytes — `IPC_OK`, then the EE program counter as a 32-bit
  little-endian unsigned integer, then a single byte that is `1` if emulation is
  currently paused or `0` if it is running.
- **Reply (fail):** 5 bytes — `IPC_FAIL` when no valid VM is active.

### MsgPause (0x11)

- **Request:** 5 bytes total — opcode only.
- **Reply (OK):** 5 bytes — `IPC_OK`. Returns success whether or not execution was
  already paused.
- **Reply (fail):** 5 bytes — `IPC_FAIL` when no valid VM is active.

### MsgResume (0x12)

- **Request:** 5 bytes total — opcode only.
- **Reply (OK):** 5 bytes — `IPC_OK`. Returns success whether or not execution was
  already running.
- **Reply (fail):** 5 bytes — `IPC_FAIL` when no valid VM is active.

### MsgStep (0x13)

- **Request:** 5 bytes total — opcode only.
- **Reply (OK):** 9 bytes — `IPC_OK`, then the new EE program counter as a 32-bit
  little-endian unsigned integer after the instruction has executed.
- **Reply (fail):** 5 bytes — `IPC_FAIL` when emulation is not currently paused.

### MsgSetBreakpoint (0x14)

- **Request:** 9 bytes total — opcode followed by the target EE virtual address as
  a 32-bit little-endian unsigned integer.
- **Reply (OK):** 5 bytes — `IPC_OK`. Duplicate addresses are silently ignored.
- When execution reaches the registered address the emulator pauses automatically.

### MsgClearBreakpoint (0x15)

- **Request:** 9 bytes total — opcode followed by the EE address to remove.
- **Reply (OK):** 5 bytes — `IPC_OK`.
- **Reply (fail):** 5 bytes — `IPC_FAIL` when the address was not registered.

### MsgClearAllBreakpoints (0x16)

- **Request:** 5 bytes total — opcode only.
- **Reply (OK):** 5 bytes — `IPC_OK`. Removes all PC breakpoints.

### MsgGetRegisters (0x17)

- **Request:** 5 bytes total — opcode only.
- **Reply (OK):** 145 bytes — `IPC_OK`, then the lower 32 bits of each of the 32 EE
  general-purpose registers in order (r0–r31, 128 bytes), then the program counter
  (4 bytes), then the HI register (4 bytes), then the LO register (4 bytes).
- **Reply (fail):** 5 bytes — `IPC_FAIL` when no valid VM is active.

### MsgGetRegister (0x18)

Generic single-register read for any register on either CPU.

- **Request:** 8 bytes — opcode, `cpu` (u8: 0=EE, 1=IOP), `category` (u8), `index` (u8).
- **Reply (OK):** 21 bytes — `IPC_OK`, then the 16-byte register value as a 128-bit
  little-endian integer. Registers narrower than 128 bits are zero-extended.
- **Reply (fail):** 5 bytes — `IPC_FAIL`.

EE register categories:

| Value | Name        | Width  | Notes                         |
|-------|-------------|--------|-------------------------------|
| 0     | GPR         | 128-bit| indices 0–31; 32=PC, 33=HI, 34=LO |
| 1     | CP0         | 32-bit | indices 0–31                  |
| 2     | FPR         | 32-bit | indices 0–31                  |
| 3     | FCR         | 32-bit | FP control registers          |
| 4     | VU0F        | 128-bit| VU0 float; index 32=ACC       |
| 5     | VU0I        | 32-bit | VU0 integer                   |
| 6     | GSPRIV      | 64-bit | GS private registers          |

IOP has one category (0 = GPR), indices 0–31; index 32=PC, 33=HI, 34=LO.

### MsgSetRegister (0x19)

Generic single-register write. Only valid when emulation is paused.

- **Request:** 24 bytes — opcode, `cpu` (u8), `category` (u8), `index` (u8), then the
  new register value as a 128-bit little-endian integer (16 bytes).
- **Reply (OK):** 5 bytes — `IPC_OK`.
- **Reply (fail):** 5 bytes — `IPC_FAIL` (emulation not paused, or invalid category/index).

### MsgGetEEThreads (0x1A)

- **Request:** 5 bytes — opcode only.
- **Reply (OK):** variable — `IPC_OK`, `count` (u32LE), then for each thread:

  ```
  TID      : u32LE
  PC       : u32LE   (current program counter of the thread)
  status   : u8      (0=bad, 1=run, 2=ready, 4=wait, 8=suspend, 0x10=dormant)
  wait     : u8      (0=none, 1=wakeup, 2=sema, 3=sleep, 4=delay, …)
  priority : u32LE
  entry    : u32LE   (initial entry point)
  stack_top: u32LE
  ```

  22 bytes per thread; up to 256 threads.
- **Reply (fail):** 5 bytes — `IPC_FAIL` when no valid VM is active.

### MsgGetIOPThreads (0x1B)

Same layout as `MsgGetEEThreads`; returns IOP (R3000) threads. Up to 1000 threads.

### MsgGetModules (0x1C)

- **Request:** 5 bytes — opcode only.
- **Reply (OK):** variable — `IPC_OK`, `count` (u32LE), then for each module:

  ```
  name      : 32 bytes, NUL-padded
  version   : u16LE
  text_addr : u32LE
  entry     : u32LE
  gp        : u32LE
  text_size : u32LE
  data_size : u32LE
  bss_size  : u32LE
  ```

  58 bytes per module; up to 1000 modules.
- **Reply (fail):** 5 bytes — `IPC_FAIL` when no valid VM is active.

### MsgGetStack (0x1D)

Walks the EE call stack for the currently running thread.

- **Request:** 5 bytes — opcode only.
- **Reply (OK):** variable — `IPC_OK`, `count` (u32LE), then for each frame:

  ```
  entry      : u32LE   (function start address)
  pc         : u32LE   (next instruction within this frame)
  sp         : u32LE   (stack pointer inside this frame)
  stack_size : u32LE   (frame size in bytes)
  ```

  16 bytes per frame; up to 1024 frames.
- **Reply (fail):** 5 bytes — `IPC_FAIL` when no valid VM is active.

### MsgStepInto (0x1E)

Sets a temporary breakpoint at the next logical instruction and resumes. The client
must poll `MsgGetProgramCounter` until `IsPaused == 1` to learn the new PC.

- **Request:** 5 bytes — opcode only.
- **Reply (OK):** 5 bytes — `IPC_OK` (returned immediately; execution is still running).
- **Reply (fail):** 5 bytes — `IPC_FAIL` if emulation is not currently paused.

Branch and delay-slot handling mirrors the GUI debugger:
- Unconditional branch → breakpoint at branch target.
- Conditional branch taken → breakpoint at branch target.
- Conditional branch not taken → breakpoint at `PC + 8` (skip delay slot).
- Default → breakpoint at `PC + 4`.

### MsgStepOver (0x1F)

Like `MsgStepInto` but skips over function calls (`jal`/`jalr`).

- **Request:** 5 bytes — opcode only.
- **Reply (OK):** 5 bytes — `IPC_OK` immediately.
- **Reply (fail):** 5 bytes — `IPC_FAIL` if emulation is not currently paused.

For linked branches (`jal`/`jalr`) the breakpoint is placed at `PC + 8` so the entire
call is skipped. Other branches follow the same rules as `MsgStepInto`.

### MsgStepOut (0x20)

Walks the call stack to find the caller and sets a temporary breakpoint there.

- **Request:** 5 bytes — opcode only.
- **Reply (OK):** 5 bytes — `IPC_OK` immediately.
- **Reply (fail):** 5 bytes — `IPC_FAIL` if emulation is not paused or the stack
  cannot be walked (fewer than 2 frames).

### MsgGetSymbol (0x21)

Looks up the EE function symbol whose range includes the given address.

- **Request:** 9 bytes — opcode followed by the query address (u32LE).
- **Reply (OK):** variable — `IPC_OK`, `start_address` (u32LE), then the NUL-terminated
  function name as a UTF-8 string.
- **Reply (fail):** 5 bytes — `IPC_FAIL` when no symbol overlaps the address or no
  valid VM is active.

---

## Batch Mode

All opcodes 0x10–0x21 are **single-command only** — they must not appear inside a
multi-command batch request. If any of them is encountered after another command
has already been processed in the same request buffer, the server returns `IPC_FAIL`
for the entire batch. This restriction exists because these opcodes mutate or observe
CPU state where strict ordering guarantees are required.

---

## Breakpoint Integration

`MsgSetBreakpoint` wires into the existing PCSX2 `CBreakPoints` infrastructure so
the interpreter's normal breakpoint-check path triggers the pause automatically.
The high-level step opcodes (0x1E–0x20) use temporary stepping breakpoints (the same
mechanism as the built-in GUI debugger's Step Into/Over/Out buttons), including the
skip-first mechanism to avoid re-triggering on a breakpoint that is already at the
current PC.

---

## Backwards Compatibility

- All new opcodes are numbered above 0x0F and do not overlap with any existing opcode.
- Clients that never send the new opcodes see no behavioural change.
- Builds that do not include this change return `IPC_FAIL` for any unrecognised
  opcode, which is already the documented behaviour for `MsgUnimplemented (0xFF)`.

---

## Testing Checklist

- [ ] `MsgGetProgramCounter` returns a consistent PC value while a game is running
- [ ] `MsgPause` / `MsgResume` toggle the paused flag reported by `MsgGetProgramCounter`
- [ ] `MsgStep` while paused advances the PC; returns `IPC_FAIL` while running
- [ ] `MsgSetBreakpoint` + `MsgResume` pauses at the correct address
- [ ] `MsgClearBreakpoint` removes the breakpoint; `MsgClearAllBreakpoints` removes all
- [ ] `MsgGetRegisters` returns a 145-byte reply; `r0` is always zero
- [ ] `MsgGetRegister(0, 0, 0)` returns EE r0 (always zero, 16 bytes)
- [ ] `MsgGetRegister(1, 0, 0)` returns IOP r0 (16 bytes, zero-extended)
- [ ] `MsgSetRegister` while paused changes the register value; `IPC_FAIL` while running
- [ ] `MsgGetEEThreads` returns correct thread count, status, and PC fields
- [ ] `MsgGetIOPThreads` returns IOP thread list
- [ ] `MsgGetModules` lists loaded IOP modules with correct names and addresses
- [ ] `MsgGetStack` returns at least one frame while paused inside a function
- [ ] `MsgStepInto` + poll `MsgGetProgramCounter` follows branch targets correctly
- [ ] `MsgStepOver` skips function calls (`jal` → PC + 8)
- [ ] `MsgStepOut` resumes and pauses at the return site of the current function
- [ ] `MsgGetSymbol` returns name and start address for a known function address
- [ ] `MsgGetSymbol` returns `IPC_FAIL` for an address with no symbol
- [ ] All opcodes 0x10–0x21 return `IPC_FAIL` gracefully when called inside a batch

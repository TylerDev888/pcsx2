# PINE Protocol Extension — Debugger Commands

## Summary

This proposal extends the PINE IPC protocol with eight new opcodes that expose EE CPU
debugger state to external tools. The additions allow a remote client to pause and
resume emulation, single-step the EE CPU, manage PC breakpoints, and inspect the
full integer register file, all through the existing PINE socket without any
separate out-of-band channel.

All new opcodes are numbered 0x10–0x17, above the current ceiling of 0x0F, so no
existing opcode values change and clients that do not use the new opcodes are
completely unaffected.

---

## Motivation

The existing PINE protocol covers memory read/write and basic emulator metadata.
External tools that need a full debugger experience — breakpoint managers, code
coverage harnesses, automated test runners — currently have no way to control
execution or inspect CPU state without a separate, custom socket. Adding these
opcodes makes PINE a self-contained debugger transport.

---

## New Opcodes

| Value | Name                     | Purpose                                              |
|-------|--------------------------|------------------------------------------------------|
| 0x10  | `MsgGetProgramCounter`   | Read the current EE program counter and paused flag  |
| 0x11  | `MsgPause`               | Halt EE execution at the next instruction boundary   |
| 0x12  | `MsgResume`              | Resume EE execution                                  |
| 0x13  | `MsgStep`                | Execute exactly one EE instruction while paused      |
| 0x14  | `MsgSetBreakpoint`       | Register a PC-execution breakpoint at an EE address  |
| 0x15  | `MsgClearBreakpoint`     | Remove the breakpoint at a given EE address          |
| 0x16  | `MsgClearAllBreakpoints` | Remove every breakpoint registered through PINE      |
| 0x17  | `MsgGetRegisters`        | Read all 32 EE GPRs (lower 32 bits), PC, HI, and LO |

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
- **Reply (fail):** 5 bytes — `IPC_FAIL` when the address was not registered as a
  PINE breakpoint.

### MsgClearAllBreakpoints (0x16)

- **Request:** 5 bytes total — opcode only.
- **Reply (OK):** 5 bytes — `IPC_OK`. Removes only the breakpoints that were
  registered through PINE, leaving any breakpoints set via the GUI debugger intact.

### MsgGetRegisters (0x17)

- **Request:** 5 bytes total — opcode only.
- **Reply (OK):** 145 bytes — `IPC_OK`, then the lower 32 bits of each of the 32 EE
  general-purpose registers in order (r0–r31, 128 bytes), then the program counter
  (4 bytes), then the HI register (4 bytes), then the LO register (4 bytes).
- **Reply (fail):** 5 bytes — `IPC_FAIL` when no valid VM is active.

---

## Batch Mode

All eight new opcodes are **single-command only** — they must not appear inside a
multi-command batch request. If any of them is encountered after another command
has already been processed in the same request buffer, the server returns `IPC_FAIL`
for the entire batch. This restriction exists because these opcodes mutate or observe
CPU state where strict ordering guarantees are required.

---

## Breakpoint Integration

Breakpoints registered via `MsgSetBreakpoint` are tracked internally and also wired
into the existing PCSX2 breakpoint infrastructure so that the interpreter's normal
breakpoint-check path handles the pause automatically. When `MsgClearBreakpoint` or
`MsgClearAllBreakpoints` is called, only the PINE-registered breakpoints are removed;
any breakpoints set through the GUI debugger remain unaffected.

---

## Backwards Compatibility

- All new opcodes are numbered above 0x0F and do not overlap with any existing opcode.
- Clients that never send the new opcodes see no behavioural change.
- Builds that do not include this change return `IPC_FAIL` for any unrecognised
  opcode, which is already the documented behaviour for `MsgUnimplemented (0xFF)`.

---

## Testing Checklist

- [ ] `MsgGetProgramCounter` returns a consistent PC value while a game is running
- [ ] `MsgPause` halts execution; a subsequent `MsgGetProgramCounter` reply has
      `IsPaused == 1`
- [ ] `MsgResume` restarts execution; a subsequent `MsgGetProgramCounter` reply has
      `IsPaused == 0`
- [ ] `MsgStep` while paused advances the PC to the next instruction
- [ ] `MsgStep` while running returns `IPC_FAIL`
- [ ] `MsgSetBreakpoint` followed by `MsgResume` pauses at the correct address
- [ ] `MsgClearBreakpoint` removes the breakpoint; execution no longer pauses there
- [ ] `MsgClearAllBreakpoints` removes all PINE-managed breakpoints
- [ ] `MsgGetRegisters` returns a 145-byte reply; `r0` is always zero (hardwired)
- [ ] All new opcodes return `IPC_FAIL` gracefully when called inside a batch

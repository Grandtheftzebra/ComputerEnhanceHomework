# Simulating Memory

This is the continuation of [SIMULATING_MOVS.md](SIMULATING_MOVS.md). It records
the changes needed to decode and execute the memory operations in Listings 51 and
52.

## The central idea

Decoding a memory operand must preserve its **addressing recipe**, not calculate
an address immediately.

For example, `[bp + si + 4]` cannot be resolved while decoding because BP and SI
do not receive values until execution. The decoder therefore stores:

- `modValue`: whether a displacement exists and how it was encoded.
- `rmValue`: which register combination forms the base address.
- `addressValue`: the signed displacement, or the complete address for direct
  addressing.

`addressValue` is `int32_t` so both signed displacements and unsigned 16-bit
direct addresses fit safely.

The operand's role remains independent of this recipe:

- The `reg` field is always a register.
- The `r/m` field is a register only when `mod == 11`; otherwise it is memory.
- The direction bit only decides which operand is the source and which is the
  destination.

## Decoder changes

`Operand` now carries the data needed by execution:

```text
Register  -> registerIndex
Immediate -> immediateValue
Memory    -> modValue + rmValue + addressValue
```

`DecodedInstruction::uses16Bit` preserves the `w` bit. Execution needs this to
know whether a memory operation reads or writes one byte or two.

`decodeRmOperand` still produces the assembly text, but it also fills the
structured memory operand and updates `instruction.size` when it consumes
displacement bytes. The structured operand is for execution; the string is only
for printing.

Immediate-to-memory MOV (`C6`/`C7`) was added for instructions such as:

```asm
mov word [1000], 1
```

For this encoding:

- The opcode is identified without consuming the `w` bit.
- The ModR/M `reg` field is an opcode extension and must be `000`.
- The current implementation rejects `mod == 11`, because this decoder is used
  for the memory form.

## Memory state

Simulation owns a zero-initialized 1 MiB byte array:

```text
memory[0] ... memory[1,048,575]
```

Each element holds one byte. A 16-bit word therefore occupies two adjacent
elements.

## Calculating an effective address

`CalculateAddress` runs during execution, using the current register values.
The direct-address special case is:

```text
mod = 00 and r/m = 110 -> addressValue
```

Every other `r/m` value selects this recipe:

| `r/m` | Effective address before displacement |
|---|---|
| `000` | BX + SI |
| `001` | BX + DI |
| `010` | BP + SI |
| `011` | BP + DI |
| `100` | SI |
| `101` | DI |
| `110` | BP |
| `111` | BX |

The signed `addressValue` displacement is added afterward.

The register array uses encoding order:

```text
0 AX, 1 CX, 2 DX, 3 BX, 4 SP, 5 BP, 6 SI, 7 DI
```

The `r/m` value is **not** a register-array index. It describes one of the
recipes above.

## Reading and writing bytes

`GetValueAt` and `SetValueAt` receive an already-calculated address. Both reject
negative addresses and validate the complete one- or two-byte range before
touching the memory array.

The 8086 is little-endian. For the word `0x1234`, memory contains:

| Address | Byte |
|---|---|
| `address` | `0x34` (low byte) |
| `address + 1` | `0x12` (high byte) |

Writing splits the word as follows:

- Low byte: preserve bits 0-7 with `source & 0xFF`.
- High byte: move bits 8-15 down with `source >> 8`.

Reading reverses that operation: the high byte is shifted left by eight and
combined with the low byte.

Endianness controls the order of **bytes in memory**. It does not reverse the
bits inside a byte.

## MOV execution

MOV now follows one simple flow:

1. `ReadOperandValue` obtains the source from a register, immediate, or memory.
2. A register destination updates the register array.
3. A memory destination calculates its effective address and calls
   `SetValueAt`.
4. An immediate destination is rejected.

MOV does not modify flags.

## Verification

The binary/source pairs for Listings 51 and 52 were checked before simulator
work began.

### Listing 51

The first instructions write words at addresses 1000, 1002, 1004, and 1006.
After BX becomes 1000, `[bx + 4]` resolves to address 1004 and overwrites the
word there with 10.

The subsequent reads correctly produce:

```text
BX = 1
CX = 2
DX = 10
BP = 4
```

### Listing 52

The initialization loop writes:

```text
[1000] = 0
[1002] = 2
[1004] = 4
```

The second loop reads those words and accumulates `0 + 2 + 4`.

Final relevant state:

```text
BX = 6
CX = 4
DX = 6
BP = 1000
SI = 6
Flags = Z
```

The repeated backward `jnz` instructions also confirm that instruction sizes
and jump targets remain correct.

## Current boundaries

- Listings 51 and 52 use word operations. Correct AL/AH-style 8-bit register
  reads and writes remain separate work.
- Memory destinations are currently supported by MOV; arithmetic still requires
  a register destination.
- Negative effective addresses currently throw. Real 8086 offset arithmetic
  wraps at 16 bits.
- Segment-based physical-address calculation is not simulated yet.

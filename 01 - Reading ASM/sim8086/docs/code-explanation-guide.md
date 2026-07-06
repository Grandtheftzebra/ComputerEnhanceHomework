# sim8086 — Code Explanation Guide

A complete walkthrough of [main.cpp](../src/main.cpp): how the program decodes 8086 machine code, handles jumps, and simulates registers, flags, and memory.

> **Convention used in this guide:** all byte values are shown in **binary first**, with hex in parentheses — e.g. `0b10001001 (0x89)` — because the decoder works on bit patterns, not hex digits.

---

## 1. What This Program Is

`sim8086` is a two-mode tool for 8086 machine code binaries:

| Mode | Command | What it does |
|------|---------|--------------|
| **Disassembler** | `sim8086 file` | Prints NASM-compatible assembly (`DecodeFile`) |
| **Simulator** | `sim8086 -exec file` | Executes the instructions, printing register/flag changes (`SimulateFile`) |
| **Simulator + dump** | `sim8086 -execS file` | Same as `-exec`, plus writes the 1MB simulated memory to `file.data` |

The overall pipeline:

```
binary file → bytes → DecodeInstruction (per instruction) → DecodedInstruction
                                                                 │
                       Disassembler: build labels → print ASM ◄──┤
                       Simulator: ExecuteInstruction → registers/flags/memory
```

Supported instructions: `mov`, `add`, `sub`, `cmp` (several encodings each), all 16 conditional jumps, and the `loop`/`loopz`/`loopnz`/`jcxz` family. Anything else throws `"Instruction is not supported."`.

<details>
<summary><strong>Q: Why does the same program have both string fields (destination/source) and Operand structs in DecodedInstruction?</strong></summary>

The strings exist for the **disassembler** — they are exactly what gets printed (`mov cx, bx`). The `Operand` structs exist for the **simulator** — executing an instruction needs structured data (which register index? what immediate value? what mod/rm for address calculation?), not text. Decoding fills in both so either mode can consume the result.

</details>

<details>
<summary><strong>Q: What happens if the decoder meets a byte it doesn't recognize?</strong></summary>

`DecodeInstruction` falls through all the `Is...` checks and throws `std::runtime_error("Instruction is not supported.")`. `main` catches it, prints `Error: ...`, and returns exit code 1. There is no attempt to skip the byte and continue — one unknown opcode aborts the whole run.

</details>

<details>
<summary><strong>Q: Is decoding done once up front in both modes?</strong></summary>

No — only the disassembler decodes everything up front (`DecodeInstructions` loops over the whole file first, because it needs *all* jump targets before it can print labels). The simulator decodes **one instruction at a time** inside its execution loop, because a jump can move the instruction pointer, so the "next instruction" isn't known until the current one executes.

</details>

---

## 2. Core Data Structures

```cpp
enum class OperandKind {Register, Immediate, Memory};

struct Operand
{
    OperandKind kind = OperandKind::Register;
    uint8_t registerIndex {};   // used when kind == Register
    uint16_t immediateValue {}; // used when kind == Immediate
    uint8_t modValue {};        // \
    uint8_t rmValue {};         //  used when kind == Memory
    int32_t addressValue {};    // /  (displacement or direct address)
};
```

An `Operand` is a tagged struct: `kind` says which fields are meaningful. For memory operands, `modValue`/`rmValue`/`addressValue` carry everything the simulator later needs to compute an effective address (see section 6.2).

```cpp
struct DecodedInstruction
{
    std::string mnemonic, destination, source; // for printing
    Operand destinationOperand, sourceOperand; // for executing
    size_t size;       // total bytes this instruction occupies
    size_t offset;     // where it starts in the file
    bool hasJumpTarget; int jumpTarget; // jumps only
    bool uses16Bit;    // the W bit: word (true) or byte (false) operation
};
```

`size` is critical: the decode loop advances by it, and jump targets are computed with it.

<details>
<summary><strong>Q: Why is addressValue an int32_t when 8086 addresses are 16-bit?</strong></summary>

Because it stores **two different things**: either an unsigned direct address (`0`–`65535`) or a **signed displacement** (`-32768`–`+32767`, e.g. `[bx - 4]`). A plain `int16_t` couldn't hold all direct addresses interpreted as unsigned, and a `uint16_t` couldn't hold negative displacements. `int32_t` holds both ranges without ambiguity.

</details>

<details>
<summary><strong>Q: Why does the instruction need both size and offset?</strong></summary>

`offset` = where the instruction starts; `size` = how many bytes it consumed. The decode loop needs `size` to find the next instruction (`i += instruction.size`). Jump handling needs both: a jump's target is `offset + size + displacement` (relative to the *next* instruction), and label placement matches label offsets against instruction `offset`s.

</details>

<details>
<summary><strong>Q: What does uses16Bit correspond to in the machine code?</strong></summary>

The **W bit** of the opcode byte. `W = 0b1` → 16-bit (word) operation using registers `ax`–`di`; `W = 0b0` → 8-bit (byte) operation using `al`–`bh`. The simulator also uses it to decide whether memory reads/writes touch 1 or 2 bytes.

</details>

---

## 3. Reading the Binary & Byte Helpers

```cpp
std::vector<uint8_t> ReadBinaryFile(const std::string& path)
```

Opens the file in binary mode, seeks to the end to get the size, seeks back, and reads everything into a `std::vector<uint8_t>`. Throws on open/read failure.

Every multi-byte or bounds-sensitive read goes through helpers:

```cpp
void ensureBytesAvailable(bytes, index, count, context)
// throws "Unexpected end of file while decoding <context>" if not enough bytes

uint16_t readU16(bytes, index, context)
{
    const uint8_t lowByte = bytes[index];
    const uint8_t highByte = bytes[index + 1];
    return static_cast<uint16_t>(lowByte) | static_cast<uint16_t>(highByte) << 8;
}
```

**The 8086 is little-endian**: the low byte comes first in memory. So the byte sequence `0b11101000 (0xE8)`, `0b00000011 (0x03)` is the 16-bit value `0b0000001111101000 (0x03E8)` = **1000**, not `0xE803`.

`readI8`/`readI16` are the signed variants — same bytes, reinterpreted as two's complement (`0b11111100 (0xFC)` as `int8_t` is **−4**).

<details>
<summary><strong>Q: The bytes 0b11101000 (0xE8) then 0b00000011 (0x03) appear in the file. What 16-bit value is that, and why?</strong></summary>

**1000** (`0x03E8`). The 8086 is little-endian, so the first byte is the **low** byte: `value = 0xE8 | (0x03 << 8) = 0x03E8 = 1000`. Reading it in file order as `0xE803` would be wrong.

</details>

<details>
<summary><strong>Q: Why does ensureBytesAvailable use two conditions: index > bytes.size() || count > bytes.size() - index?</strong></summary>

The first catches an index already past the end. The second catches a *valid* index with too few bytes remaining — e.g. index at the last byte but needing 2 bytes for a 16-bit immediate. It's written as `count > bytes.size() - index` (instead of `index + count > bytes.size()`) to avoid unsigned overflow: sizes are `size_t`, and `index + count` could wrap around for huge values, silently passing the check.

</details>

<details>
<summary><strong>Q: What is the difference between readU16 and readI16?</strong></summary>

Nothing at the byte level — `readI16` literally calls `readU16` and casts the result to `int16_t`. The difference is *interpretation*: `readU16` is for values that are addresses/immediates (unsigned), `readI16` for displacements (signed two's complement, so `0b1111111111111100 (0xFFFC)` means −4).

</details>

---

## 4. Opcode Handling — How Instructions Are Recognized and Decoded

### 4.1 Bit-pattern matching

The decoder never uses a lookup table of all 256 opcodes. Instead, each instruction family has a **predicate** that shifts/masks the first byte and compares the fixed bit pattern:

```cpp
bool IsMovRegisterMemoryToFromRegister(const uint8_t byte)
{
    return (byte >> 2) == 0b100010;   // pattern 100010dw
}
```

Shifting right by 2 discards the two variable bits (`d`, `w`), leaving only the 6 fixed opcode bits to compare. The full dispatch table:

| First byte pattern | Meaning | Hex range | Handler |
|---|---|---|---|
| `0b100010dw` | mov reg/mem ↔ reg | `0x88`–`0x8B` | `DecodeMovRegisterMemoryToFromRegister` |
| `0b1011wreg` | mov immediate → reg | `0xB0`–`0xBF` | `DecodeMovImmediateToRegister` |
| `0b1100011w` | mov immediate → reg/mem | `0xC6`–`0xC7` | `DecodeMovImmediateToRegisterMemory` |
| `0b00xxx0dw` | add/sub/cmp reg/mem ↔ reg | e.g. `0x00`–`0x03` (add) | `DecodeArithmeticRegisterMemoryToFromRegister` |
| `0b100000sw` | add/sub/cmp immediate → reg/mem | `0x80`–`0x83` | `DecodeArithmeticImmediateToRegisterMemory` |
| `0b00xxx10w` | add/sub/cmp immediate → al/ax | `0x04/05`, `0x2C/2D`, `0x3C/3D` | `DecodeArithmeticImmediateToAccumulator` |
| `0b0111cccc` | conditional jumps | `0x70`–`0x7F` | `DecodeJump` |
| `0b111000xx` | loop/loopz/loopnz/jcxz | `0xE0`–`0xE3` | `DecodeJump` |

Lowercase letters are variable bits: `d` = direction, `w` = width, `s` = sign-extend, `reg` = register code, `xxx` = arithmetic operation, `cccc` = jump condition.

For the arithmetic families, the check is a **mask compare** because the operation bits sit in the *middle* of the byte:

```cpp
// pattern 00xxx0dw: top two bits must be 00, bit 2 must be 0
return ((byte1 & 0b11000100) == 0b00000000) && (getArithmeticMnemonic(operation) != nullptr);
```

The AND keeps only the fixed positions; the operation bits `xxx = (byte1 >> 3) & 0b111` are then validated separately (`0b000`=add, `0b101`=sub, `0b111`=cmp, anything else → not this family).

### 4.2 The mod/reg/rm byte

Most instructions have a second byte with three packed fields:

```
  bit: 7 6 | 5 4 3 | 2 1 0
       mod |  reg  |  r/m
```

Extracted with shift + mask:

```cpp
const uint8_t mod = (byte2 >> 6) & 0b11;
const uint8_t reg = (byte2 >> 3) & 0b111;
const uint8_t rm  = byte2 & 0b111;
```

- **mod** — what kind of operand `r/m` is:
  - `0b00` → memory, no displacement (except the `rm = 0b110` special case)
  - `0b01` → memory + signed **8-bit** displacement
  - `0b10` → memory + signed **16-bit** displacement
  - `0b11` → `r/m` is a **register**, no memory involved
- **reg** — always a register code (or an opcode extension in immediate forms)
- **r/m** — register code (mod `0b11`) or effective-address formula index (see 4.3)

Register codes (from `getRegisterName`):

| code | `w=0` | `w=1` |
|---|---|---|
| `0b000` | al | ax |
| `0b001` | cl | cx |
| `0b010` | dl | dx |
| `0b011` | bl | bx |
| `0b100` | ah | sp |
| `0b101` | ch | bp |
| `0b110` | dh | si |
| `0b111` | bh | di |

**Worked example** — bytes `0b10001001 (0x89)`, `0b11011001 (0xD9)`:

```
byte1 = 0b100010|0|1  → mov reg/mem↔reg, d=0, w=1 (16-bit)
byte2 = 0b11|011|001  → mod=11 (register), reg=011 (bx), rm=001 (cx)
d=0 → destination is rm      → mov cx, bx
```

### 4.3 Decoding the r/m operand: `decodeRmOperand`

This is the shared workhorse — every mod/reg/rm instruction calls it. It returns the operand *string* and fills the `Operand` struct's `addressValue`, while **growing `instructionSize`** for any displacement bytes it consumes:

```cpp
if (mod == 0b11)                    return getRegisterName(rm, w);   // register, done
if (mod == 0b00 && rm == 0b110)     // SPECIAL CASE: direct 16-bit address
    → read 2 bytes, "[1000]", size += 2
else if (mod == 0b00)               → "[bx + si]"          (no displacement)
else if (mod == 0b01)               → read 1 signed byte,  "[bx + si - 4]", size += 1
else /* mod == 0b10 */              → read 2 signed bytes, "[bx + si + 1000]", size += 2
```

The effective-address base comes from a fixed table indexed by `r/m` (`getEffectiveAddressBase`):

| r/m | base formula |
|---|---|
| `0b000` | bx + si |
| `0b001` | bx + di |
| `0b010` | bp + si |
| `0b011` | bp + di |
| `0b100` | si |
| `0b101` | di |
| `0b110` | bp *(or direct address when mod = 0b00)* |
| `0b111` | bx |

When `includeMemorySize` is true, memory operands get a `byte `/`word ` prefix based on `w` — NASM needs it when the other operand is an immediate (`mov byte [bp + di], 7` would be ambiguous without it).

**Worked example** — bytes `0b10001010 (0x8A)`, `0b01100000 (0x60)`, `0b00000100 (0x04)`:

```
byte1 = 0b100010|1|0  → mov, d=1, w=0 (8-bit)
byte2 = 0b01|100|000  → mod=01 (mem + disp8), reg=100 (ah), rm=000 (bx + si)
byte3 = 0b00000100    → displacement +4
d=1 → destination is reg      → mov ah, [bx + si + 4]
```

### 4.4 The three MOV decoders

**a) Register/memory ↔ register** (`0b100010dw`) — the general form shown above. The `d` bit picks direction: `d=1` → `reg` is destination, `d=0` → `rm` is destination. The code builds both operands, then swaps assignment based on `d`.

**b) Immediate → register** (`0b1011wreg`) — the shortest form; `w` and `reg` are packed *into the first byte*, so there's no mod/reg/rm byte at all:

```cpp
const uint8_t w   = (byte1 >> 3) & 0b1;
const uint8_t reg = byte1 & 0b111;
```

Then 1 immediate byte if `w=0` (size 2), or 2 if `w=1` (size 3).
Example: `0b10111001 (0xB9)`, `0b00000011 (0x03)`, `0b00000000 (0x00)` → `w=1`, `reg=0b001` → **`mov cx, 3`**.

**c) Immediate → register/memory** (`0b1100011w`) — has a mod/reg/rm byte, but the `reg` field is an **opcode extension** that must be `0b000` (the code throws otherwise). Layout: opcode byte, mod/rm byte, displacement bytes (if any), then the immediate. That's why the immediate is read at `index + instruction.size` — `decodeRmOperand` has already advanced `size` past the displacement.

Example — `0b11000110 (0xC6)`, `0b00000011 (0x03)`, `0b00000111 (0x07)`:

```
byte1 = 0b1100011|0    → mov imm→reg/mem, w=0
byte2 = 0b00|000|011   → mod=00, reg=000 (ok), rm=011 (bp + di)
byte3 = 0b00000111     → immediate 7
→ mov byte [bp + di], 7
```

### 4.5 The three arithmetic decoders

`add`, `sub`, and `cmp` share encodings — a 3-bit **operation field** distinguishes them:

```cpp
const char* getArithmeticMnemonic(const uint8_t operation)
{
    case 0b000: return "add";
    case 0b101: return "sub";
    case 0b111: return "cmp";
    default:    return nullptr;   // any other op → "not supported"
}
```

**a) Register/memory ↔ register** (`0b00xxx0dw`) — structurally identical to MOV form (a); the operation field `xxx` lives in the opcode byte. `DecodeArithmeticRegisterMemoryToFromRegister` is a near-copy of the MOV decoder with the mnemonic looked up first.

Example — `0b00000001 (0x01)`, `0b11001011 (0xCB)`:

```
byte1 = 0b00|000|0|0|1 → op=000 (add), d=0, w=1
byte2 = 0b11|001|011   → mod=11, reg=001 (cx), rm=011 (bx)
d=0 → destination is rm       → add bx, cx
```

**b) Immediate → register/memory** (`0b100000sw`) — here the operation field moves into the **reg slot of byte 2** (that slot is free since the source is an immediate). The `s` bit is new:

```cpp
if (s == 1 || w == 0)   // read 8-bit immediate
else                    // read 16-bit immediate
```

- `s=0, w=1` → full 16-bit immediate follows
- `s=1, w=1` → only **1 byte** follows, sign-extended to 16 bits (saves a byte for small values — NASM emits this whenever the immediate fits in a signed byte)

Example — `0b10000011 (0x83)`, `0b11000011 (0xC3)`, `0b00001010 (0x0A)`:

```
byte1 = 0b100000|1|1  → arith imm→reg/mem, s=1, w=1
byte2 = 0b11|000|011  → mod=11, op=000 (add), rm=011 (bx)
byte3 = 0b00001010    → immediate 10 (one byte, sign-extended)
→ add bx, 10
```

**c) Immediate → accumulator** (`0b00xxx10w`) — a compact form hardwired to `al`/`ax`, so no mod/reg/rm byte:
Example — `0b00000101 (0x05)`, `0b11101000 (0xE8)`, `0b00000011 (0x03)` → op=`0b000` (add), w=1, immediate `0x03E8` = 1000 → **`add ax, 1000`**.

### 4.6 The dispatch loop

```cpp
DecodedInstruction DecodeInstruction(const std::vector<uint8_t>& bytes, const size_t index)
{
    if      (IsMovRegisterMemoryToFromRegister(byte1)) ... 
    else if (IsMovImmediateToRegister(byte1)) ...
    ... 
    else if (IsJump(byte1)) ...
    else throw std::runtime_error("Instruction is not supported.");
    instruction.offset = index;
}

std::vector<DecodedInstruction> DecodeInstructions(const std::vector<uint8_t>& bytes)
{
    for (size_t i = 0; i < bytes.size();)
    {
        DecodedInstruction instruction = DecodeInstruction(bytes, i);
        i += instruction.size;              // ← variable-length stepping
        instructions.push_back(instruction);
    }
}
```

8086 instructions are **variable length** (2–6 bytes in this subset), so the loop can't step by a constant — each decoded instruction reports its own `size`, and the loop advances exactly that far. A single mis-decoded size would desynchronize everything after it.

<details>
<summary><strong>Q: Decode by hand: 0b10001011 (0x8B), 0b00101110 (0x2E), 0b00000101 (0x05), 0b00000000 (0x00). What instruction is this and how many bytes?</strong></summary>

```
byte1 = 0b100010|1|1  → mov reg/mem↔reg, d=1, w=1
byte2 = 0b00|101|110  → mod=00, reg=101 (bp), rm=110
mod=00 + rm=110 → SPECIAL CASE: direct address, read 16-bit little-endian
bytes 3–4 = 0x0005 = 5
```

**`mov bp, [5]`**, 4 bytes total. Without the special case, `mod=00, rm=110` would have meant `[bp]` with no displacement — the 8086 designers stole that slot for direct addressing (a bare `[bp]` is instead encoded as `mod=01, rm=110` with displacement 0).

</details>

<details>
<summary><strong>Q: Why does IsMovRegisterMemoryToFromRegister shift the byte right by 2 before comparing?</strong></summary>

The pattern is `0b100010dw` — the low 2 bits (`d`, `w`) vary per instruction. `byte >> 2` throws them away, leaving just the 6 fixed opcode bits, which are compared against `0b100010`. This single check matches all four concrete bytes `0x88`, `0x89`, `0x8A`, `0x8B`.

</details>

<details>
<summary><strong>Q: What does the d bit do, and what happens in the code when d = 0 vs d = 1?</strong></summary>

`d` (direction) says which operand is the destination. `d=1` → the `reg` field is the destination (`reg ← r/m`); `d=0` → the `r/m` field is the destination (`r/m ← reg`). In code, both operand strings/structs are built identically, then an `if (d == 1)` swaps which one is assigned to `instruction.destination` vs `instruction.source`.

</details>

<details>
<summary><strong>Q: In mov immediate→reg/mem (0b1100011w), the reg field of byte 2 must be 0b000. Why does a field exist at all if it must be zero?</strong></summary>

The source is an immediate, so the `reg` slot isn't needed for a register — Intel reuses it as an **opcode extension** to distinguish instructions sharing the same first byte (e.g. the `0b100000sw` family uses that same slot for add/sub/cmp). For `0b1100011w`, only extension `0b000` means `mov`; the code enforces this with a throw so a mis-decode fails loudly instead of printing wrong assembly.

</details>

<details>
<summary><strong>Q: With s=1, w=1 (byte 0b10000011 / 0x83), how many immediate bytes follow, and what is 0b11111111 (0xFF) supposed to mean there?</strong></summary>

One byte, and it is **sign-extended** to 16 bits: `0b11111111` means −1 (i.e. `0xFFFF` as a word). This is a size optimization — `add bx, -1` needs 3 bytes instead of 4.

In the code, the sign-extension happens when storing the immediate for the simulator: `static_cast<int8_t>` reinterprets the byte as signed (`0b11111111` → −1), then widening to `int16_t` sign-extends (−1 → `0b1111111111111111`), and the final `uint16_t` cast keeps that bit pattern (`0xFFFF`). For `w = 0` the raw byte is stored as-is, since the operation itself is 8-bit.

</details>

<details>
<summary><strong>Q: Why does decodeRmOperand take instructionSize by reference?</strong></summary>

Because it *consumes a variable number of bytes* (0, 1, or 2 displacement bytes) and the caller must know how many. The caller starts `size` at 2 (opcode + mod/rm byte already consumed), passes it in by reference, and `decodeRmOperand` bumps it for each displacement byte. Afterwards the caller reads any immediate at `index + instruction.size` — which is only correct because size already includes the displacement.

</details>

<details>
<summary><strong>Q: How does DecodeInstructions know where the second instruction starts?</strong></summary>

It doesn't know in advance — instructions are variable-length. It decodes the first instruction, which reports its own `size`, then advances `i += instruction.size`. Instruction boundaries are *discovered*, not looked up. This is also why an unsupported opcode aborts everything: with one bad size, every later "instruction" would be decoded from a misaligned offset.

</details>

---

## 5. Jump Handling

### 5.1 Recognizing jumps

```cpp
const char* getJumpMnemonic(const uint8_t byte1)
{
    static const char* conditionalJumps[16] = {
        "jo", "jno", "jb", "jnb", "je", "jnz", "jbe", "ja",
        "js", "jns", "jp", "jnp", "jl", "jnl", "jle", "jg"
    };

    if ((byte1 & 0b11110000) == 0b01110000)      // pattern 0b0111cccc
        return conditionalJumps[byte1 & 0b00001111];

    switch (byte1)
    {
        case 0b11100000: return "loopnz";  // 0xE0
        case 0b11100001: return "loopz";   // 0xE1
        case 0b11100010: return "loop";    // 0xE2
        case 0b11100011: return "jcxz";    // 0xE3
        default: return nullptr;
    }
}
```

All 16 conditional jumps share the high nibble `0b0111`; the **low 4 bits index directly into the mnemonic table**. So `0b01110101 (0x75)` → index `0b0101` = 5 → `jnz`. The table order isn't arbitrary — it matches Intel's condition-code encoding, where each even/odd pair is a condition and its negation (`jo`/`jno`, `jb`/`jnb`, …).

`IsJump` is just `getJumpMnemonic(byte1) != nullptr` — the lookup doubles as the predicate.

### 5.2 Decoding: relative displacement → absolute target

All these jumps are 2 bytes: opcode + **signed 8-bit displacement** relative to the instruction *after* the jump (the 8086 has already advanced IP past the jump when it applies the displacement).

```cpp
DecodedInstruction DecodeJump(const std::vector<uint8_t>& bytes, const size_t index)
{
    const int8_t displacement = readI8(bytes, index + 1, "jump displacement");
    ...
    instruction.size = 2;
    instruction.hasJumpTarget = true;
    instruction.jumpTarget = static_cast<int>(index) + static_cast<int>(instruction.size)
                           + static_cast<int>(displacement);
}
```

**`jumpTarget = offset + size + displacement`** — converting relative to absolute once, at decode time, so both the label builder and the simulator can use a plain file offset.

**Worked example** — `0b01110101 (0x75)`, `0b11111000 (0xF8)` at offset 12:

```
0b01110101 → 0b0111 prefix, condition 0b0101 → jnz
0b11111000 → as int8_t = −8
jumpTarget = 12 + 2 + (−8) = 6
```

The jump lands at offset 6 — a backward loop.

### 5.3 Labels for disassembly

Raw output like `jnz -8` isn't valid NASM and isn't readable. The disassembler converts targets to labels in two passes:

```cpp
std::map<size_t, std::string> BuildJumpLabels(instructions, fileSize)
{
    // 1. collect every instruction start offset
    // 2. for each jump: keep its target only if it lands exactly on an
    //    instruction boundary (or == fileSize, i.e. "end of program")
    // 3. assign "label0", "label1", ... in ascending offset order
}
```

Validation matters: a target of `−3`, or one landing in the *middle* of an instruction, gets no label (the raw displacement string stays as destination). Because `targetOffsets` is a `std::set` (sorted, deduplicated), labels are numbered top-to-bottom and two jumps to the same place share one label.

`ApplyJumpLabels` then rewrites each jump's `destination` string from the raw displacement to the label name. Finally `DecodeFile` prints a `labelN:` line before any instruction whose offset has a label — plus one special check *after* the loop for a label at `fileSize` (a jump to the very end of the program).

**Full example** — this loop program:

```
offset  bytes (binary)                                      asm
0       0b10111001 0b00000011 0b00000000                    mov cx, 3
3       0b10111011 0b11101000 0b00000011                    mov bx, 1000
6       0b10000011 0b11000011 0b00001010                    add bx, 10
9       0b10000011 0b11101001 0b00000001                    sub cx, 1
12      0b01110101 0b11111000                               jnz (disp −8 → target 6)
```

Target 6 is a valid instruction start → it becomes `label0`, and the output is:

```asm
bits 16

mov cx, 3
mov bx, 1000

label0:
add bx, 10
sub cx, 1
jnz label0
```

### 5.4 Executing a jump in the simulator

```cpp
else if (instruction.mnemonic == "jnz")
{
    if (!zeroFlag)
    {
        instructionPointer = instruction.jumpTarget;
    }
}
```

By the time this runs, `SimulateFile` has already advanced `instructionPointer` past the jump (`instructionPointer += instruction.size`). If the zero flag is **set** (last result was zero) the jump does nothing — execution naturally continues at the next instruction. If it's **clear**, the IP is overwritten with the precomputed absolute target, and the next loop iteration decodes from there. Only `jnz` is implemented in the simulator; the other 19 jump mnemonics decode and disassemble fine but throw `"Unsupported mnemonic"` under `-exec`.

<details>
<summary><strong>Q: The byte 0b01111100 (0x7C) starts an instruction. Which jump is it? (Try using the table indexing rule before peeking.)</strong></summary>

High nibble `0b0111` → conditional jump. Low nibble `0b1100` = 12 → `conditionalJumps[12]` = **`jl`** (jump if less, signed: SF ≠ OF).

</details>

<details>
<summary><strong>Q: A jnz at offset 20 has displacement byte 0b00000101 (0x05). Where does it jump?</strong></summary>

`jumpTarget = 20 + 2 + 5 = 27`. The `+2` is the jump's own size — displacement is relative to the instruction *after* the jump, because real hardware has already advanced IP past it before applying the displacement.

</details>

<details>
<summary><strong>Q: Why is the displacement read with readI8 instead of just taking bytes[index + 1]?</strong></summary>

The displacement is **signed** — backward jumps (the common case for loops) have negative displacements. `0b11111000 (0xF8)` must mean −8, not 248. `readI8` casts through `int8_t` to get two's-complement interpretation (and also bounds-checks the read).

</details>

<details>
<summary><strong>Q: When does a jump target NOT get a label in the disassembly?</strong></summary>

When it fails validation in `BuildJumpLabels`: the target is negative, or it doesn't land exactly on any instruction's start offset (and isn't exactly `fileSize`). A displacement landing mid-instruction would produce assembly that can't reassemble to the same bytes, so those keep the raw numeric displacement as their destination string instead.

</details>

<details>
<summary><strong>Q: Why does BuildJumpLabels accept a target equal to fileSize when no instruction lives there?</strong></summary>

A jump can legally target the byte *after* the last instruction — "jump to end of program". There's no instruction at that offset to print a label above, which is why `DecodeFile` has a separate check after the print loop: `labels.find(fileSize)` prints the trailing label at the very bottom.

</details>

<details>
<summary><strong>Q: In the simulator, why doesn't executing a taken jnz need to add the jump's size to the target?</strong></summary>

Because the target was already made **absolute** at decode time (`jumpTarget = offset + size + displacement`). Execution just assigns `instructionPointer = jumpTarget` — all the relative-addressing math happened once, in `DecodeJump`.

</details>

<details>
<summary><strong>Q: Trace the loop from section 5.3 under -exec. How many times does add bx, 10 run, and what are the final cx and bx?</strong></summary>

`cx` counts 3 → 2 → 1 → 0. Each `sub cx, 1` updates the zero flag; `jnz` re-runs the loop while `cx ≠ 0`. So `add bx, 10` runs **3 times**: `bx = 1000 + 30 = 1030 (0x406)`, `cx = 0`, and the final flags show `Z` set (last `sub` produced zero, which is what lets `jnz` fall through).

</details>

---

## 6. Simulating Memory

### 6.1 The memory itself

```cpp
std::vector<uint8_t> memory(1024 * 1024);   // in SimulateFile
```

One flat, zero-initialized **1 MB** byte array — the full 8086 address space (20-bit addresses). No segmentation is simulated; the effective address is used directly as an index. Note the *program bytes are not in this memory* — the simulator keeps code (`bytes`) and data (`memory`) as separate arrays, unlike a real 8086 where both share one address space.

### 6.2 Computing the effective address: `CalculateAddress`

The runtime twin of `getEffectiveAddressBase` from the decoder. Where the decoder produced the *string* `"[bx + si + 4]"`, this computes the *number* using current register values:

```cpp
int32_t CalculateAddress(const std::array<uint16_t, 8>& registers, const Operand& operand)
{
    // direct 16-bit address special case (mod=00, rm=110)
    if (operand.modValue == 0b00 && operand.rmValue == 0b110) return operand.addressValue;

    switch (operand.rmValue)
    {
        case 0b000: return registers[3] + registers[6] + operand.addressValue; // bx + si
        case 0b001: return registers[3] + registers[7] + operand.addressValue; // bx + di
        case 0b010: return registers[5] + registers[6] + operand.addressValue; // bp + si
        case 0b011: return registers[5] + registers[7] + operand.addressValue; // bp + di
        case 0b100: return registers[6] + operand.addressValue;                // si
        case 0b101: return registers[7] + operand.addressValue;                // di
        case 0b110: return registers[5] + operand.addressValue;                // bp
        case 0b111: return registers[3] + operand.addressValue;                // bx
    }
}
```

This is why the decoder stored `modValue`, `rmValue`, and `addressValue` in the `Operand`: the address **can't** be computed at decode time, because it depends on register values *at the moment the instruction executes*. `[bx + si + 4]` is a formula, not a location — the same instruction can hit a different address every loop iteration.

### 6.3 Reading and writing bytes: `GetValueAt` / `SetValueAt`

```cpp
uint16_t GetValueAt(const int32_t memoryAddress, const std::vector<uint8_t>& memory, const bool is16Bit)
{
    if (memoryAddress < 0) throw ...;
    if (address + bytesNeeded > memory.size()) throw ...;   // 1 or 2 bytes

    const uint8_t byte1 = memory[address];
    if (!is16Bit) return byte1;
    const uint8_t byte2 = memory[address + 1];
    return byte1 | (byte2 << 8);        // NOTE: 8086 is little endian
}
```

`SetValueAt` is the mirror image — split the 16-bit value and store low byte first:

```cpp
memory[address]     = source & 0xFF;   // 0xFF = 0b11111111 → keep low 8 bits
if (is16Bit)
    memory[address + 1] = source >> 8; // high byte second
```

**Worked example** — executing `mov word [1000], 258`:
258 = `0b0000000100000010 (0x0102)`, so:

```
memory[1000] = 0b00000010 (0x02)   ← low byte first
memory[1001] = 0b00000001 (0x01)   ← high byte second
```

Reading a *byte* from 1000 gives 2; reading a *word* gives `0b00000010 | (0b00000001 << 8)` = 258.

### 6.4 One entry point for all operand reads: `ReadOperandValue`

```cpp
switch (operand.kind)
{
    case OperandKind::Register:  return registers[operand.registerIndex];
    case OperandKind::Immediate: return operand.immediateValue;
    case OperandKind::Memory:    return GetValueAt(CalculateAddress(registers, operand), memory, is16Bit);
}
```

This is where the `Operand` tagged struct pays off: `ExecuteInstruction` never cares what kind of source it has — register, constant, and memory all funnel through this one function and come out as a `uint16_t`. Only *writes* still branch on kind (registers and memory are written differently, and writing to an immediate is an error).

### 6.5 Dumping memory: `-execS`

```cpp
if (saveFile)
{
    const std::string outputPath = path + ".data";
    std::ofstream output(outputPath, std::ios::binary);
    output.write(reinterpret_cast<const char*>(memory.data()), memory.size());
}
```

After the program finishes, the entire 1 MB array is written verbatim to `<input>.data`. This is how you inspect what the simulated program wrote — e.g. the Computer Enhance homework where a program draws an image into memory and you open the dump as raw pixel data.

<details>
<summary><strong>Q: Registers hold bx = 1000, si = 4. The instruction is mov ax, [bx + si + 2] (operand has rmValue 0b000, addressValue 2). What address is read, and which memory bytes make up ax?</strong></summary>

`CalculateAddress`: rm `0b000` → `registers[3] + registers[6] + 2` = 1000 + 4 + 2 = **1006**. Then `GetValueAt(1006, memory, true)` reads two bytes little-endian: `ax = memory[1006] | (memory[1007] << 8)` — memory[1006] becomes the **low** byte of ax.

</details>

<details>
<summary><strong>Q: Why can't the decoder compute the memory address itself and just store a number in the Operand?</strong></summary>

Because effective addresses depend on **register values at execution time**. `[bx + si]` inside a loop hits a different address every iteration as `si` changes. Decode time only knows the *formula* (mod/rm) and the *constant part* (displacement) — so that's exactly what `Operand` stores (`modValue`, `rmValue`, `addressValue`), and `CalculateAddress` plugs in live register values later.

</details>

<details>
<summary><strong>Q: After executing mov word [1000], 258 — what exact byte values are at memory[1000] and memory[1001], in binary?</strong></summary>

258 = `0b0000000100000010 (0x0102)`.
`memory[1000] = 0b00000010 (0x02)` (low byte), `memory[1001] = 0b00000001 (0x01)` (high byte). Little-endian: low byte at the lower address. If you dumped memory with `-execS` and looked at the file, you'd see `02 01` in that order.

</details>

<details>
<summary><strong>Q: How does the simulator decide whether a memory access touches 1 byte or 2?</strong></summary>

From `instruction.uses16Bit` — which is the **W bit** captured at decode time. It's threaded through `ReadOperandValue` → `GetValueAt` / `SetValueAt` as `is16Bit`, selecting `bytesNeeded = 1 or 2` for both the bounds check and the actual access.

</details>

<details>
<summary><strong>Q: The direct-address check in CalculateAddress is mod == 0b00 && rm == 0b110. What would go wrong without it?</strong></summary>

`rm = 0b110` would fall into the switch and compute `registers[5] + addressValue` — i.e. **bp + address**. So `mov ax, [1000]` with `bp = 500` would wrongly read address 1500. The special case mirrors the decoder's: for `mod=00, rm=110` the stored `addressValue` *is* the full absolute address, not a displacement to add to bp.

</details>

<details>
<summary><strong>Q: Is the simulated program's code visible in the 1 MB memory array?</strong></summary>

No. The code stays in the separate `bytes` vector; `memory` starts all zeros and only holds what the program explicitly writes. A real 8086 has code and data in the same physical memory (self-modifying code is possible); this simulator's split is a simplification that costs nothing for these homework programs.

</details>

---

## 7. Execution: Registers, Flags, and the Trace

### 7.1 Register file

```cpp
/* Register Table:
 * 0 = AX, 1 = CX, 2 = DX, 3 = BX
 * 4 = SP, 5 = BP, 6 = SI, 7 = DI
 */
std::array<uint16_t, 8> registers {};
```

Eight 16-bit registers, indexed by the same 3-bit codes the machine code uses — so `registers[operand.registerIndex]` needs no translation. Note there is **no separate 8-bit register handling**: the simulator always reads/writes the full 16-bit slot, so byte-sized register ops (`mov al, 5`) would not behave like real hardware (and `ah`/`ch`/`dh`/`bh` indexes would hit the wrong slots entirely). The homework programs stick to 16-bit ops, so this never fires in practice.

### 7.2 `ExecuteInstruction`

Dispatch is by mnemonic string:

- **`mov`** — read source via `ReadOperandValue`, then write to register (`registers[index] = source`) or memory (`SetValueAt(CalculateAddress(...))`). No flags are touched — real 8086 `mov` doesn't affect flags either.
- **`add` / `sub`** — both operands read via `ReadOperandValue`, result computed in 16-bit unsigned arithmetic (wrapping is automatic with `uint16_t`), flags updated, result written to the destination register:

```cpp
const uint16_t result = destinationValue - sourceValue;   // sub
zeroFlag = result == 0;
signFlag = (result >> 15) & 0b1;    // top bit = sign bit
registers[instruction.destinationOperand.registerIndex] = result;
```

- **`cmp`** — *identical to sub, minus the final write-back*. It computes the subtraction purely to set flags. This is the entire mechanism behind conditional jumps: `cmp cx, 0` followed by `jnz` means "jump if cx ≠ 0".
- **`jnz`** — covered in section 5.4.

Destination for arithmetic must currently be a register (memory destinations throw). Only Z and S flags are modeled — real 8086 also has carry, overflow, parity, and auxiliary-carry, which this subset of jumps/homework doesn't need yet.

### 7.3 The simulation loop and change trace

```cpp
while (instructionPointer < bytes.size())
{
    const std::array<uint16_t, 8> beforeRegisters = registers;   // snapshot
    ...
    const DecodedInstruction instruction = DecodeInstruction(bytes, instructionPointer);
    instructionPointer += instruction.size;    // advance FIRST
    ExecuteInstruction(memory, registers, instruction, instructionPointer, zeroFlag, signFlag);
    // then diff snapshot vs current, print "; bx:0x0->0x3e8" style annotations
}
```

Two things to notice:

1. **IP advances before execution.** This matches real hardware (IP points past the current instruction while it executes) and is what makes the jump math work: a taken `jnz` *overwrites* the already-advanced IP; a not-taken one leaves it pointing at the next instruction.
2. **The trace is diff-based.** Registers are snapshotted before execution; after, any register whose value changed is printed as `name:0xbefore->0xafter` (in hex), and flags likewise as e.g. `flags:->Z`. Unchanged state prints nothing, keeping the trace readable.

Sample output for the section 5.3 loop:

```
mov cx, 3 ; cx:0x0->0x3
mov bx, 1000 ; bx:0x0->0x3e8
add bx, 10 ; bx:0x3e8->0x3f2
sub cx, 1 ; cx:0x3->0x2
jnz label0
...
sub cx, 1 ; cx:0x1->0x0 flags:->Z
jnz label0
```

<details>
<summary><strong>Q: Why is cmp implemented as a copy of sub with one line removed, and which line?</strong></summary>

The removed line is the write-back: `registers[...] = result;`. On the 8086, `cmp` performs a subtraction *only to set flags* and discards the result — it answers "how do these compare?" without destroying the destination. That's exactly what the code does: compute `destinationValue - sourceValue`, set `zeroFlag`/`signFlag`, write nothing.

</details>

<details>
<summary><strong>Q: registers[1] (cx) holds 1 and the simulator executes sub cx, 1. What are the resulting zeroFlag and signFlag, and what will a following jnz do?</strong></summary>

`result = 1 - 1 = 0` → `zeroFlag = true` (result == 0), `signFlag = false` (bit 15 is 0). The following `jnz` checks `if (!zeroFlag)` — zeroFlag is set, so it does **not** jump; the loop exits.

</details>

<details>
<summary><strong>Q: cx holds 0 and the simulator executes sub cx, 1. What is the result in binary, and which flags are set?</strong></summary>

`uint16_t` arithmetic wraps: `0 - 1 = 0b1111111111111111 (0xFFFF)` = 65535 unsigned / −1 signed. `zeroFlag = false`, `signFlag = true` (bit 15 = 1). The trace would print `cx:0x0->0xffff flags:->S`.

</details>

<details>
<summary><strong>Q: Why does the loop advance instructionPointer BEFORE calling ExecuteInstruction instead of after?</strong></summary>

Because `ExecuteInstruction` may *change* it (a taken `jnz` assigns `instructionPointer = jumpTarget`). If the loop advanced afterwards, it would add the jump's size on top of the target and land 2 bytes past the label. Advancing first also mirrors real 8086 behavior, where IP points past the current instruction during execution — the same convention the displacement encoding assumes.

</details>

<details>
<summary><strong>Q: Why snapshot ALL registers before each instruction instead of having ExecuteInstruction report what it changed?</strong></summary>

Simplicity — 8 registers is a 16-byte copy, effectively free. Diffing snapshot vs current after execution catches every change with zero bookkeeping inside `ExecuteInstruction`, which stays focused on semantics. Having each operation report its writes would be more code for no observable benefit at this scale.

</details>

<details>
<summary><strong>Q: Name two things this simulator deliberately does NOT model compared to a real 8086.</strong></summary>

Any two of:

- **8-bit register halves** (`al`/`ah` etc.) — registers are always accessed as full 16-bit values.
- **Most flags** — only Z and S; no carry, overflow, parity, or auxiliary carry.
- **Segmentation** — addresses index the 1 MB array directly; no CS/DS/SS/ES.
- **Code and data sharing one memory** — program bytes live outside the simulated memory.
- **18 of the 19 other jumps** — only `jnz` executes; the rest decode but throw under `-exec`.

</details>

---

## 8. `main` and the CLI

```cpp
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2)                                        DecodeFile(argv[1]);
        else if (argc == 3 && std::string(argv[1]) == "-exec")  SimulateFile(argv[2]);
        else if (argc == 3 && std::string(argv[1]) == "-execS") SimulateFile(argv[2], true);
        else { std::cerr << "Usage: sim8086 [-exec] <binary-file>\n"; return 1; }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
```

One `try/catch` wraps everything — every error path in the program (unreadable file, truncated instruction, unsupported opcode, out-of-bounds memory access) is a thrown `std::runtime_error` that lands here, prints one line to stderr, and exits with code 1. No error handling is scattered through the decode/execute logic beyond the throws themselves.

<details>
<summary><strong>Q: Why route all errors through exceptions instead of returning error codes from the decoders?</strong></summary>

Every decoder failure is fatal anyway (a variable-length stream can't recover from one bad decode — see section 4.6), so there's nothing useful a caller could do with an error code except propagate it. A throw with a descriptive message (`"Unexpected end of file while decoding 16-bit immediate"`) skips all the plumbing and centralizes reporting in one `catch`.

</details>

<details>
<summary><strong>Q: What's the difference between -exec and -execS?</strong></summary>

Both simulate. `-execS` additionally passes `saveFile = true` to `SimulateFile`, which after execution writes the full 1 MB memory array to `<input-path>.data` — used to inspect data the simulated program wrote to memory (e.g. dumping a framebuffer the program drew).

</details>

---

## Quick Reference Card

**Field extraction idioms:**

```cpp
(byte >> 6) & 0b11    // mod   (bits 7-6)
(byte >> 3) & 0b111   // reg   (bits 5-3)
byte & 0b111          // r/m   (bits 2-0)
(byte >> 1) & 0b1     // d or s bit
byte & 0b1            // w bit
```

**mod values:** `0b00` mem/no-disp (`rm=0b110` → direct addr) · `0b01` mem+disp8 · `0b10` mem+disp16 · `0b11` register

**Arithmetic op field:** `0b000` add · `0b101` sub · `0b111` cmp

**Jump math:** `target = jumpOffset + 2 + signedDisplacement`

**Little-endian:** low byte at the lower address, always — file immediates, displacements, and simulated memory alike.

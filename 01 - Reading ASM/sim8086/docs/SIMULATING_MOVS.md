# From Disassembler to Simulator — Registers, Arithmetic, and Tracing

This document begins with Part 1 ("Simulating Non-memory MOVs") and follows the
simulator through arithmetic, flags, and per-instruction tracing. It explains both
*what* the current code does and *why* each piece exists.

---

## 1. The one idea that matters: decoding vs. simulating

These are two **different jobs**, and conflating them is what made it confusing.

### Decoding (what we already had)
A **decoder** reads bytes and tells you *what instruction they represent*. It turns

```
10001001 11000110   →   "mov si, ax"
```

That's it. A decoder is **read-only** and **stateless**. It looks at bytes and
describes them. It has no idea what `ax` currently holds, because it has no `ax` at all.

> Our original program was a pure decoder. That's why running it on listing_0044
> printed the assembly back perfectly — and *also* why it could not tell us the final
> register values. It had nowhere to put them.

### Simulating (what we added)
A **simulator** doesn't just describe instructions — it **runs** them. To run
`mov si, ax`, you must actually *have* an `ax` with a value in it, and you must
*write* that value into `si`. That requires **state**: a memory of what every
register holds right now.

So the leap from Part 0 to Part 1 is exactly this:

> **A decoder describes. A simulator remembers and changes.**

The test is no longer only "does the disassembly come out right?" (that's decoding).
It is also "what state changes after each instruction, and where does the machine end
up?" Only execution can answer that — and that's why listing_0044 exists: it chains values
(`mov sp, ax` then later `mov dx, sp`), which only produces the right answer if you
carried each value forward through real state.

---

## 2. The architecture: a pipeline in two halves

Everything we built fits this shape:

```
        BYTES                 STRUCTURED INSTRUCTION              CPU STATE
   (raw machine code)   →     (DecodeInstruction + Operands)  →  (registers[8] + Z/S)
        DECODE                                                     EXECUTE + TRACE
```

- **Decode half** (already existed, we extended it): bytes → a `DecodeInstruction`
  describing the operation and its operands.
- **Execute half** (new): take that structured instruction, apply it to the register
  and flag state, then report the resulting changes.

The bridge between the two halves is the **`Operand`** type — and getting that bridge
right was the heart of this part.

---

## 3. The bridge: meaning vs. rendering

This is the subtlety that took the longest to grasp, so here it is plainly.

Our decoder originally stored operands as **strings**:

```cpp
result.destination = "sp";   // a string
result.source      = "ax";   // a string
```

Strings are perfect for **printing** (disassembly). But they are useless for
**executing**, because to run `mov sp, ax` the simulator needs to know:

- *which register* is the destination (the number 4, for `sp`)
- *which register* is the source (the number 0, for `ax`)

If all we have is the string `"ax"`, we'd have to *parse it back* into the number 0 —
but the decoder **already knew** that number (it came straight from the instruction
bits) and then threw it away when it built the string!

> **The key realization:** a string is a *rendering* of an operand. The simulator
> needs the *meaning* of the operand. When you find yourself wanting to parse data
> back out of a string you produced yourself, that's the signal you discarded
> structure too early.

### The fix: a tagged struct

So we kept the **meaning** alongside the rendering. An operand isn't really "a string"
— it's one of a few *kinds* of thing, each carrying different data:

```cpp
enum class OperandKind { Register, Immediate, Memory };

struct Operand
{
    OperandKind kind           = OperandKind::Register;
    uint8_t     registerIndex  = 0;   // meaningful when kind == Register
    uint16_t    immediateValue = 0;   // meaningful when kind == Immediate
};
```

This is called a **tagged union** / **discriminated record**: a `kind` field (the tag)
tells you which of the other fields is meaningful.

- `mov ax, 1` → destination is a **Register** (index 0), source is an **Immediate** (1).
- `mov sp, ax` → destination is a **Register** (4), source is a **Register** (0).
- `mov [bx], cx` → destination is **Memory** (we don't execute these yet — we just
  refuse to mislabel them).

The decoder now fills in **both** representations: the structured `Operand` (for
execution) *and* the string (for printing). Same decoded value, two consumers.

---

## 4. Two rules that kept the decode logic clean

While wiring `Operand` into the MOV decoders, two distinctions removed all the
confusion:

### Rule 1 — operand *kind* is decided by the bits, not by direction

- The `reg` field of an instruction is **always** a register.
- The `r/m` field is a register **only when `mod == 0b11`**; otherwise it's memory.
- Neither of these depends on the `d` (direction) bit.

### Rule 2 — `d` only chooses *roles* (which is destination, which is source)

`d` never changes *what* an operand is — it only swaps which operand goes into the
"destination" slot vs. the "source" slot.

So the clean pattern is: **build each operand fully first, then let `d` route them.**

```cpp
// reg field is unconditionally a register
Operand regAsOperand;
regAsOperand.kind = OperandKind::Register;
regAsOperand.registerIndex = reg;

// r/m field is a register only when mod == 0b11, else memory
Operand rmAsOperand;
if (mod == 0b11) { rmAsOperand.kind = OperandKind::Register; rmAsOperand.registerIndex = rm; }
else             { rmAsOperand.kind = OperandKind::Memory; }

// d just picks who is destination and who is source
if (d == 1) { result.destinationOperand = regAsOperand; result.sourceOperand = rmAsOperand; }
else        { result.destinationOperand = rmAsOperand;  result.sourceOperand = regAsOperand; }
```

> Note: there is **no Immediate** in the reg/mem-to/from-reg form. Immediates only
> appear in the "immediate to register" instruction. Mixing them up was a real bug
> we hit — kinds come from *which field and what `mod` says*, nothing else.

---

## 5. The CPU state: registers and flags

The registers are stored as:

```cpp
std::array<uint16_t, 8> registers{};   // {} → all 8 start at 0
```

Why an array and not a hash map? Because the **keys are dense small integers 0–7**
(the 3-bit register codes the decoder already produces). When keys are "small integers
from 0 to N," an array is the natural container:

- `registers[4]` is a single memory access — no hashing, no heap nodes, no pointer chasing.
- A hash map would be pure overhead for 8 fixed entries.

And there's an elegant alignment: the `reg16[]` name table is indexed by the **same**
code, so `registers[i]` and `reg16[i]` line up — index 4 is `sp` in both. The array
*mirrors the hardware*: a CPU register file really is a small fixed bank addressed by
number.

The simulator also tracks the two flags needed by the currently supported arithmetic:

```cpp
bool zeroFlag{};
bool signFlag{};
```

- **Zero (`Z`)** is set when an arithmetic result is zero.
- **Sign (`S`)** is copied from bit 15 of the 16-bit result.

These are separate booleans rather than a complete 8086 FLAGS register because no other
flags are simulated yet. The instruction pointer and `al`/`ah` half-register behavior are
also still outside the current scope.

---

## 6. The execute half, piece by piece

### Piece 1 — read a value *from* an operand

"Given an operand, what is its value right now?" depends entirely on its kind:

```cpp
uint16_t ReadOperandValue(const std::array<uint16_t, 8>& registers, const Operand& operand)
{
    switch (operand.kind)
    {
        case OperandKind::Register:  return registers[operand.registerIndex];
        case OperandKind::Immediate: return operand.immediateValue;
        case OperandKind::Memory:    throw std::runtime_error("memory operands not supported yet");
        default:                     throw std::runtime_error("unknown operand kind");
    }
}
```

Note this is a **read** helper — `registers` is `const`. You can *read* an immediate,
but you can't *write* to one (`mov 5, ax` is nonsense), which is why writing needs a
separate path with a guard.

### Piece 2 — execute one instruction

`ExecuteInstruction` currently supports `mov`, `add`, `sub`, and `cmp`:

```cpp
void ExecuteInstruction(std::array<uint16_t, 8>& registers,
                        const DecodeInstruction& instruction,
                        bool& zeroFlag,
                        bool& signFlag)
{
    if (instruction.destinationOperand.kind != OperandKind::Register)
        throw std::runtime_error("destination must be a register");

    if (instruction.mnemonic == "mov")
    {
        registers[instruction.destinationOperand.registerIndex] =
            ReadOperandValue(registers, instruction.sourceOperand);
    }
    else if (instruction.mnemonic == "add")
    {
        const uint16_t result =
            ReadOperandValue(registers, instruction.destinationOperand) +
            ReadOperandValue(registers, instruction.sourceOperand);

        zeroFlag = result == 0;
        signFlag = (result >> 15) & 0b1;
        registers[instruction.destinationOperand.registerIndex] = result;
    }
    // sub computes and stores destination - source.
    // cmp performs the same subtraction only to update the flags.
    else if (instruction.mnemonic == "sub" /* ... */) { /* ... */ }
    else if (instruction.mnemonic == "cmp" /* ... */) { /* ... */ }
    else
    {
        throw std::runtime_error("Unsupported mnemonic: " + instruction.mnemonic);
    }
}
```

The destination guard rejects memory writes because memory simulation is not implemented.
Unknown mnemonics also fail loudly instead of silently producing incorrect state.

The instruction behavior is:

- `mov` copies the source into the destination and leaves the flags unchanged.
- `add` and `sub` write their 16-bit result and update `Z` and `S`.
- `cmp` calculates `destination - source` and updates `Z` and `S`, but does not write
  the result back to the destination.

### Piece 3 — the per-instruction trace loop

```cpp
void SimulateFile(const std::string& path)
{
    const std::vector<DecodeInstruction> instructions = ReadAndDecode(path);
    std::array<uint16_t, 8> registers{};

    bool zeroFlag{};
    bool signFlag{};

    for (const DecodeInstruction& instruction : instructions)
    {
        const std::array<uint16_t, 8> beforeRegisters = registers;
        const bool beforeZeroFlag = zeroFlag;
        const bool beforeSignFlag = signFlag;

        ExecuteInstruction(registers, instruction, zeroFlag, signFlag);

        PrintInstruction(instruction, false);

        bool printedChange = false;
        for (size_t i = 0; i < registers.size(); ++i)
        {
            if (beforeRegisters[i] == registers[i]) continue;

            if (!printedChange)
            {
                std::cout << " ;";
                printedChange = true;
            }

            std::cout << ' ' << getRegisterName(static_cast<uint8_t>(i), 1)
                      << ":0x" << std::hex << beforeRegisters[i]
                      << "->0x" << registers[i] << std::dec;
        }

        const std::string beforeFlags = FormatFlags(beforeZeroFlag, beforeSignFlag);
        const std::string afterFlags = FormatFlags(zeroFlag, signFlag);

        if (beforeFlags != afterFlags)
        {
            if (!printedChange) std::cout << " ;";
            std::cout << " flags:" << beforeFlags << "->" << afterFlags;
        }

        std::cout << '\n';
    }
}
```

The outer loop executes instructions in file order. Before each execution it snapshots
the current registers and flags. After execution, it compares the snapshots with the
new state and prints only what changed on the same line as the instruction.

`printedChange` ensures the ` ;` separator is printed once, before the first change.
The inner register loop scans all eight registers, skips unchanged values, prints changed
values in hexadecimal, and restores `std::dec` afterward so the stream's number format
does not leak into later output. `FormatFlags` converts the two booleans into `Z`, `S`,
`ZS`, or an empty string so flag transitions can use the same `before->after` format.

---

## 7. The `-exec` switch

We added a second *mode*. Without the flag, the program disassembles (old behavior);
with `-exec`, it simulates.

```cpp
if (argc == 2)                                              DecodeFile(argv[1]);   // disassemble
else if (argc == 3 && std::string(argv[1]) == "-exec")      SimulateFile(argv[2]); // simulate
else                                                        /* usage error */;
```

Two traps we hit and fixed:

1. **`-exec` is fixed-position and goes *first*:** `sim8086 -exec listing`. So the flag
   is `argv[1]` and the **filename is `argv[2]`** (we briefly passed `argv[1]` by
   mistake, which tried to open a file literally named `-exec`).
2. **String comparison:** `argv[1] == "-exec"` compares **pointers**, not text, and is
   always false. Wrapping one side in `std::string` (`std::string(argv[1]) == "-exec"`)
   does a real character comparison.

> Also remember to **rebuild** before testing — editing source but running a stale
> `.exe` made it look like the flag did nothing.

---

## 8. Register order in the trace

The inner trace loop scans the register array from index 0 through 7. That is **encoding
order**, because the array is indexed directly by the 8086's 3-bit register codes:

- `ax, cx, dx, bx, sp, bp, si, di`

Usually only one destination register changes per instruction, so this ordering is
rarely visible. If an instruction changes multiple registers in the future, their trace
entries will appear in encoding order. The storage array must remain in encoding order;
any friendlier presentation order should be applied only while printing.

---

## 9. Mental model to carry forward

1. **Decode = describe; execute = remember + change.** Keep the two halves separate.
2. **Operands carry meaning, not just text.** Preserve structure (`kind`, index,
   value) at the moment you first have it, instead of re-parsing strings later.
3. **State lives in an array indexed by register code** — the same code that indexes
   the name table. The data structure mirrors the hardware.
4. **Trace state transitions.** Snapshot before execution, execute once, then compare
   before and after state to report only changes.
5. **Arithmetic owns flag updates.** `add`, `sub`, and `cmp` update `Z` and `S`; `mov`
   leaves them unchanged, and `cmp` does not store its subtraction result.
6. **Guard loudly** on anything not yet supported (memory operands, unknown mnemonics)
   so future gaps fail visibly instead of corrupting state.
7. **Build only what the listings exercise.** We still defer half-register behavior,
   memory execution, the instruction pointer, and the rest of FLAGS.

This same architecture extends cleanly:
- **More arithmetic** → additional execution cases and the remaining affected flags.
- **Memory operands** → implement reads/writes for `OperandKind::Memory`.
- **Jumps** → add an instruction pointer and let execution choose the next instruction
  instead of always relying on the range-based loop.

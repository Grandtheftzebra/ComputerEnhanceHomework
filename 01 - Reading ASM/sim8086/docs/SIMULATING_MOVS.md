# From Disassembler to Simulator — Simulating Non-Memory MOVs

This document explains *what* we built in Part 1 ("Simulating Non-memory MOVs") and
*why* each piece exists. If the concept didn't fully click while coding, read this
top to bottom — it's written to build the idea up from scratch.

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

The test is no longer "does the disassembly come out right?" (that's decoding). It's
"after running all the instructions, what's in each register?" Only execution can
answer that — and that's why listing_0044 exists: it chains values
(`mov sp, ax` then later `mov dx, sp`), which only produces the right answer if you
carried each value forward through real state.

---

## 2. The architecture: a pipeline in two halves

Everything we built fits this shape:

```
        BYTES                 STRUCTURED INSTRUCTION              CPU STATE
   (raw machine code)   →     (DecodeInstruction + Operands)  →  (registers[8])
        DECODE                                                     EXECUTE
```

- **Decode half** (already existed, we extended it): bytes → a `DecodeInstruction`
  describing the operation and its operands.
- **Execute half** (new): take that structured instruction and *apply* it to the
  register file, mutating state.

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

## 5. The CPU state: why an array, not a hash map

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

> **Minimum state for this part:** just the 8 registers. We deliberately deferred
> `al`/`ah` half-registers, the FLAGS register, and the instruction pointer — none of
> the MOVs in listings 0043/0044 need them. Don't build what the listings don't exercise.

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

### Piece 2 — execute one MOV

A MOV is "compute the source value, store it into the destination register":

```cpp
void ExecuteInstruction(std::array<uint16_t, 8>& registers, const DecodeInstruction& instruction)
{
    if (instruction.destinationOperand.kind != OperandKind::Register)
        throw std::runtime_error("destination must be a register");
    if (instruction.mnemonic != "mov")
        throw std::runtime_error("only mov supported currently");

    const uint16_t source = ReadOperandValue(registers, instruction.sourceOperand);   // read first
    registers[instruction.destinationOperand.registerIndex] = source;                 // then write
}
```

Two deliberate guards:
- The destination **must** be a register (you can only store *into* a register).
- The mnemonic must be `mov` (arithmetic isn't executed yet — fail loudly instead of
  doing the wrong thing).

And the order matters in principle: **read the source before writing the destination.**

### Piece 3 — the loop + the register dump

```cpp
void SimulateFile(const std::string& path)
{
    const std::vector<DecodeInstruction> instructions = ReadAndDecode(path);
    std::array<uint16_t, 8> registers{};   // all start at 0

    for (const DecodeInstruction& instruction : instructions)
        ExecuteInstruction(registers, instruction);

    // dump every register: name + hex + decimal
    for (size_t i = 0; i < registers.size(); ++i)
        std::cout << "      " << getRegisterName(static_cast<uint8_t>(i), 1) << ": 0x"
                  << std::hex << std::setfill('0') << std::setw(4) << registers[i]
                  << " (" << std::dec << registers[i] << ")\n";
}
```

This is the whole simulator: start state at zero, fold each instruction over the
state, print the result.

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

## 8. Encoding order vs. display order

The register dump came out as `ax, cx, dx, bx, ...` instead of `ax, bx, cx, dx, ...`.
That's **not a bug** — every *value* was correct. It's two different orderings:

- **Encoding order** (`ax, cx, dx, bx, sp, bp, si, di`) is the actual 3-bit register
  code. Storage and execution **must** use this, because that's what the bytes mean.
- **Display order** (`ax, bx, cx, dx, ...`) is just a friendlier order for humans.

The fix is to reorder the **iteration when printing**, never the array itself:

```cpp
static const uint8_t displayOrder[8] = {0, 3, 1, 2, 4, 5, 6, 7}; // ax,bx,cx,dx,sp,bp,si,di
```

> Keep these two orderings separate in your head: encoding order is forced by the
> hardware; presentation order is a cosmetic choice.

---

## 9. Mental model to carry forward

1. **Decode = describe; execute = remember + change.** Keep the two halves separate.
2. **Operands carry meaning, not just text.** Preserve structure (`kind`, index,
   value) at the moment you first have it, instead of re-parsing strings later.
3. **State lives in an array indexed by register code** — the same code that indexes
   the name table. The data structure mirrors the hardware.
4. **Guard loudly** on anything not yet supported (memory operands, non-mov mnemonics)
   so future gaps fail visibly instead of corrupting state.
5. **Build only what the listings exercise.** We deferred halves/flags/IP and left a
   `Memory` tripwire — scaffolding for later parts without overbuilding now.

This same architecture extends cleanly:
- **Arithmetic (`add`/`sub`/`cmp`)** → new `ExecuteInstruction` cases + a FLAGS register.
- **Memory MOVs** → flesh out the `OperandKind::Memory` path you already stubbed.
- **Jumps** → an instruction pointer that execution updates instead of a plain loop.
```


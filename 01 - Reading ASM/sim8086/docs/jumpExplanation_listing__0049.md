# Listing 0049: instruction pointer and `jnz`

This document walks through the current simulator while it executes:

```asm
bits 16

mov cx, 3
mov bx, 1000
loop_start:
add bx, 10
sub cx, 1
jnz loop_start
```

All instruction bytes are displayed as 8 bits. Instruction-pointer and register values are displayed as 16 bits. Decimal values are included beside them when useful, but hexadecimal is not used.

## 1. What `loop_start:` means

`loop_start:` is a label. It gives a name to the location of the next instruction:

```asm
loop_start:
add bx, 10
```

The label produces no machine-code bytes and does nothing during execution. Here, it names byte offset `6`, which is:

```text
0000000000000110
```

The assembler uses that address when translating `jnz loop_start`. The label name does not exist in the finished binary.

## 2. The complete binary file

The file contains 14 bytes:

| Decimal byte offset | 16-bit offset | 8-bit byte | Meaning |
|---:|---|---|---|
| 0 | `0000000000000000` | `10111001` | `mov cx, immediate` opcode |
| 1 | `0000000000000001` | `00000011` | low byte of `3` |
| 2 | `0000000000000010` | `00000000` | high byte of `3` |
| 3 | `0000000000000011` | `10111011` | `mov bx, immediate` opcode |
| 4 | `0000000000000100` | `11101000` | low byte of `1000` |
| 5 | `0000000000000101` | `00000011` | high byte of `1000` |
| 6 | `0000000000000110` | `10000011` | arithmetic-with-immediate opcode |
| 7 | `0000000000000111` | `11000011` | `add`, destination `bx` |
| 8 | `0000000000001000` | `00001010` | immediate `10` |
| 9 | `0000000000001001` | `10000011` | arithmetic-with-immediate opcode |
| 10 | `0000000000001010` | `11101001` | `sub`, destination `cx` |
| 11 | `0000000000001011` | `00000001` | immediate `1` |
| 12 | `0000000000001100` | `01110101` | `jnz` opcode |
| 13 | `0000000000001101` | `11111000` | signed 8-bit displacement `-8` |

The address immediately after the file is decimal `14`:

```text
0000000000001110
```

The instructions therefore occupy these byte ranges:

| Instruction | Start IP | Bytes | Size | Default next IP |
|---|---|---|---:|---|
| `mov cx, 3` | `0000000000000000` | `10111001 00000011 00000000` | 3 | `0000000000000011` |
| `mov bx, 1000` | `0000000000000011` | `10111011 11101000 00000011` | 3 | `0000000000000110` |
| `add bx, 10` | `0000000000000110` | `10000011 11000011 00001010` | 3 | `0000000000001001` |
| `sub cx, 1` | `0000000000001001` | `10000011 11101001 00000001` | 3 | `0000000000001100` |
| `jnz loop_start` | `0000000000001100` | `01110101 11111000` | 2 | `0000000000001110` |

## 3. Who calculates what?

There are three distinct jobs:

1. The **assembler** translates `jnz loop_start` and stores the displacement byte.
2. The **decoder** reads that byte and calculates the absolute `jumpTarget`.
3. The **executor** reads the zero flag and decides whether to replace the instruction pointer with `jumpTarget`.

The decoder does not decide whether the jump is taken. The executor does not calculate where the label was. Keeping those responsibilities separate makes the control flow easier to follow.

## 4. How the assembler gets displacement `-8`

The jump target, `loop_start`, is byte offset `6`:

```text
0000000000000110
```

The `jnz` instruction begins at byte offset `12` and occupies 2 bytes. Consequently, the default next IP is byte offset `14`:

```text
  0000000000001100   current IP: 12
+ 0000000000000010   instruction size: 2
= 0000000000001110   next IP: 14
```

An 8086 short conditional jump stores its displacement relative to that next IP, not relative to the beginning of the jump instruction:

```text
displacement = target - next IP
             = 6 - 14
             = -8
```

In 16-bit binary, the same subtraction is:

```text
  0000000000000110   target: 6
+ 1111111111110010   two's-complement representation of -14
= 1111111111111000   result: -8
```

The instruction only has room for an 8-bit displacement, so the stored byte is:

```text
11111000
```

To verify that `11111000` represents `-8`:

```text
11111000   original negative value
00000111   invert every bit
00001000   add 1, giving magnitude 8
```

Therefore the assembled `jnz` instruction is:

```text
01110101 11111000
```

## 5. Entering `SimulateFile`

The relevant setup is:

```cpp
const std::vector<uint8_t> bytes = ReadBinaryFile(path);
std::array<uint16_t, 8> registers {};
size_t instructionPointer { 0 };

bool zeroFlag {};
bool signFlag {};
```

Line by line:

- `ReadBinaryFile` reads the 14 bytes shown above exactly once.
- `registers {}` initializes every simulated register to sixteen zero bits.
- `instructionPointer { 0 }` points to the first byte of the file.
- `zeroFlag {}` and `signFlag {}` both begin as false, represented here as `0`.

Initial state:

```text
IP = 0000000000000000
CX = 0000000000000000
BX = 0000000000000000
ZF = 0
SF = 0
```

## 6. The execution-loop lines

The control-flow core of `SimulateFile` is:

```cpp
while (instructionPointer < bytes.size())
{
    const DecodeInstruction instruction = decodeInstruction(bytes, instructionPointer);
    instructionPointer += instruction.size;

    ExecuteInstruction(registers, instruction, instructionPointer, zeroFlag, signFlag);
}
```

Each iteration performs three important operations:

1. Decode the instruction beginning at the current IP.
2. Move IP to the default next instruction.
3. Execute the instruction. A taken jump may overwrite that default IP.

The register and flag snapshots surrounding these lines are only used to print changes. They do not affect execution.

## 7. First pass through the program

### Iteration 1: `mov cx, 3`

The `while` condition checks:

```text
0000000000000000 < 0000000000001110
```

This is true.

`decodeInstruction(bytes, instructionPointer)` is called with IP `0000000000000000`. It reads:

```text
10111001 00000011 00000000
```

It returns `mov cx, 3` with size `3`. `SimulateFile` applies the default IP movement:

```text
  0000000000000000
+ 0000000000000011
= 0000000000000011
```

`ExecuteInstruction` then writes this value to `cx`:

```text
CX = 0000000000000011
```

The instruction does not alter IP, so the next iteration begins at IP `0000000000000011`.

### Iteration 2: `mov bx, 1000`

The decoder reads:

```text
10111011 11101000 00000011
```

The two immediate bytes are stored low byte first. Combining the high byte followed by the low byte gives:

```text
BX = 0000001111101000
```

The instruction size is `3`, so IP moves:

```text
  0000000000000011
+ 0000000000000011
= 0000000000000110
```

That new IP is the location named `loop_start` in the assembly source.

### Iteration 3: `add bx, 10`

At IP `0000000000000110`, the decoder reads:

```text
10000011 11000011 00001010
```

It returns `add bx, 10` with size `3`. Before execution, `SimulateFile` advances IP:

```text
  0000000000000110
+ 0000000000000011
= 0000000000001001
```

`ExecuteInstruction` performs:

```text
  0000001111101000   BX: 1000
+ 0000000000001010   immediate: 10
= 0000001111110010   BX: 1010
```

The result is not zero, so `ZF = 0`.

### Iteration 4: `sub cx, 1`

At IP `0000000000001001`, the decoder reads:

```text
10000011 11101001 00000001
```

It returns `sub cx, 1` with size `3`. `SimulateFile` advances IP:

```text
  0000000000001001
+ 0000000000000011
= 0000000000001100
```

`ExecuteInstruction` performs:

```text
  0000000000000011   CX: 3
- 0000000000000001   immediate: 1
= 0000000000000010   CX: 2
```

The result is not zero, so `ZF = 0`.

### Iteration 5: decoding `jnz`

IP is now `0000000000001100`, so `decodeInstruction` begins at decimal byte offset `12`.

#### Inside `decodeInstruction`

This line reads the opcode byte:

```cpp
const uint8_t byte1 = bytes[index];
```

The values are:

```text
index = 0000000000001100
byte1 = 01110101
```

The earlier instruction checks do not match this byte. `isJump(byte1)` does match because `01110101` is the defined opcode for `jnz`. Therefore `decodeInstruction` calls:

```cpp
instruction = decodeJump(bytes, index);
```

#### Inside `decodeJump`

`ensureBytesAvailable` verifies that two bytes remain. They are:

```text
01110101 11111000
```

The opcode is read from `bytes[index]`:

```text
bytes[0000000000001100] = 01110101
```

The displacement is read from `bytes[index + 1]`:

```text
index + 1 = 0000000000001101
bytes[0000000000001101] = 11111000
```

`readI8` interprets `11111000` as a signed 8-bit value, so `displacement` is `-8`.

`decodeJump` sets:

```text
mnemonic      = jnz
size          = 0000000000000010
hasJumpTarget = true
```

It then evaluates the current `jumpTarget` expression:

```cpp
result.jumpTarget = static_cast<int>(index)
                  + static_cast<int>(result.size)
                  + static_cast<int>(displacement);
```

Displayed as 16-bit binary arithmetic:

```text
  0000000000001100   index: 12
+ 0000000000000010   size: 2
= 0000000000001110   default next IP: 14
+ 1111111111111000   signed displacement: -8
= 0000000000000110   jumpTarget: 6
```

The carry beyond the 16 displayed bits is discarded. `decodeJump` returns this instruction to `decodeInstruction`, which records the instruction's starting offset as `0000000000001100` and returns it to `SimulateFile`.

### Iteration 5: advancing and executing `jnz`

Back in `SimulateFile`, this line always performs the default movement first:

```cpp
instructionPointer += instruction.size;
```

Therefore IP temporarily becomes:

```text
  0000000000001100
+ 0000000000000010
= 0000000000001110
```

Next, `ExecuteInstruction` receives the already-advanced IP.

Its `jnz` path is:

```cpp
else if (instruction.mnemonic == "jnz")
{
    if (!zeroFlag)
    {
        instructionPointer = instruction.jumpTarget;
    }
}
```

The preceding `sub` produced `CX = 0000000000000010`, so `ZF = 0`. Consequently, `!zeroFlag` is true and the executor replaces the default IP:

```text
IP before jnz execution = 0000000000001110
jumpTarget              = 0000000000000110
IP after jnz execution  = 0000000000000110
```

The next loop iteration therefore decodes the byte at offset `0000000000000110`, which is `add bx, 10` again.

`jnz` does not inspect `cx` directly and does not modify any flags. It only reads the existing zero flag produced by `sub`.

## 8. Second and third passes through the loop body

The same three instructions now repeat. The state transitions are:

| Current IP | Instruction | Result | ZF | Actual next IP |
|---|---|---|---:|---|
| `0000000000000110` | `add bx, 10` | `BX: 0000001111110010 -> 0000001111111100` | 0 | `0000000000001001` |
| `0000000000001001` | `sub cx, 1` | `CX: 0000000000000010 -> 0000000000000001` | 0 | `0000000000001100` |
| `0000000000001100` | `jnz -8` | taken | 0 | `0000000000000110` |
| `0000000000000110` | `add bx, 10` | `BX: 0000001111111100 -> 0000010000000110` | 0 | `0000000000001001` |
| `0000000000001001` | `sub cx, 1` | `CX: 0000000000000001 -> 0000000000000000` | 1 | `0000000000001100` |
| `0000000000001100` | `jnz -8` | not taken | 1 | `0000000000001110` |

On the final `jnz`, `SimulateFile` still advances IP from `0000000000001100` to `0000000000001110` before execution. This time `ZF = 1`, so `!zeroFlag` is false. `ExecuteInstruction` does not overwrite IP.

The loop condition is checked again:

```text
0000000000001110 < 0000000000001110
```

This is false, so simulation stops.

## 9. Complete instruction-pointer path

The complete path is:

```text
0000000000000000   mov cx, 3
        |
        v
0000000000000011   mov bx, 1000
        |
        v
0000000000000110   add bx, 10        <---------+
        |                                    |
        v                                    |
0000000000001001   sub cx, 1                 |
        |                                    |
        v                                    |
0000000000001100   jnz                       |
        |                                    |
        +-- ZF = 0: IP = 0000000000000110 ---+
        |
        +-- ZF = 1: keep default IP
        v
0000000000001110   end
```

Final state:

```text
IP = 0000000000001110   decimal 14
CX = 0000000000000000   decimal 0
BX = 0000010000000110   decimal 1030
ZF = 1
SF = 0
```

## 10. The compact mental model

For every instruction, `SimulateFile` does this:

```text
current IP
    |
    v
decode instruction at current IP
    |
    v
IP = IP + instruction size        default path
    |
    v
execute instruction
    |
    +-- ordinary instruction: leave IP alone
    |
    +-- taken jnz: replace IP with jumpTarget
```

For this particular jump:

```text
default IP after jnz = 0000000000001110
jumpTarget           = 0000000000000110
```

The default IP answers, "Where do we continue if no jump happens?" `jumpTarget` answers, "Where do we continue if the jump is taken?" The zero flag chooses between them.

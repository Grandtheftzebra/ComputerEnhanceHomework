# From sim8086 to Modern Assembly — Reading Real Engine Code

Companion to the [sim8086 code explanation guide](code-explanation-guide.md). Everything you decoded by hand there still exists in the machine code your engine runs today. x64 is a *direct descendant* of the 8086 — not "inspired by", but literally the same encoding scheme grown outward. This document connects the homework to real compiler output from a modern C++ game engine, with a detour into how Unity's C# reaches native code.

> **Conventions:** byte values are shown in **binary first** with hex in parentheses (`0b10001011 (0x8B)`), same as the sim8086 guide. All assembly below is **real output from MSVC 19.51 on this machine**, compiled with `cl /O2 /FA` (optimized, with assembly listing) and `dumpbin /disasm:bytes` (raw instruction bytes). You can reproduce every listing.

---

## 1. What You Already Know: the 8086 → x64 Map

| 8086 (sim8086 guide) | x64 (your engine) |
|---|---|
| 8 registers, 16-bit (`ax`…`di`) | 16 registers, 64-bit (`rax`…`rdi`, `r8`–`r15`), with 32/16/8-bit views (`eax`, `ax`, `al`) |
| mod/reg/rm byte | **Still there, bit-for-bit the same layout** (`mod` 2 bits, `reg` 3, `r/m` 3), extended by optional SIB and REX prefix bytes |
| `[bx + si + disp]` — fixed base table | `[rcx + rax*4 + disp]` — any register as base, any as index, plus a **scale** (×1/2/4/8) |
| Z and S flags; `cmp` + `jnz` | Same FLAGS register (more flags), same `cmp` + `jcc` scheme — short `jne` is **still opcode `0b01110101 (0x75)`** |
| IP, jumps relative to next instruction | RIP, same relative-jump math; data can also be RIP-relative |
| Variable-length instructions, 2–6 bytes | Variable-length, 1–15 bytes |
| You choose which registers to use | **Calling conventions** fix them: on Windows x64, args go in `rcx, rdx, r8, r9`, return in `rax` |

The concrete opcode continuity is startling. Compare these bytes from the guide's section 4 against real MSVC output below:

| Byte | In the sim8086 guide (8086) | In MSVC x64 output today |
|---|---|---|
| `0b10001011 (0x8B)` | `mov` reg/mem→reg, d=1, w=1 | `mov eax, [rcx]` — same opcode |
| `0b10000011 (0x83)` | arith imm→reg/mem, s=1, w=1 | `cmp dword ptr [rcx+rdx*4], 0` — same opcode, same sign-extended-byte trick |
| op field `0b111` | cmp (in `getArithmeticMnemonic`) | cmp — same extension table |
| `0b01110101 (0x75)` | `jnz`, rel8 | `jne`, rel8 — identical byte, identical target math |

<details>
<summary><strong>Q: MSVC emits the bytes 0b10001011 (0x8B), 0b01000001 (0x41), 0b00000100 (0x04). Decode this with the rules from the guide's section 4. (Hint: on x64, rm=001 means [rcx] and reg=000 means eax.)</strong></summary>

Same procedure as the homework:

```
0x8B = 0b100010|1|1   → mov, d=1, w=1 (register is destination)
0x41 = 0b01|000|001   → mod=01 (memory + disp8), reg=000 (eax), rm=001 ([rcx])
0x04                  → displacement +4
```

**`mov eax, [rcx+4]`** — this is exactly what MSVC emits for `return player->mana;` (mana is the second `int`, at offset 4). The only thing that changed since 1978 is what the register codes name.

</details>

<details>
<summary><strong>Q: What did x64 add to effective-address computation that the 8086's table (guide section 4.3) didn't have?</strong></summary>

Three things: **any** general register can be the base (not just bx/bp/si/di combinations), **any** register can be the index, and the index gets a **scale factor** of ×1, ×2, ×4, or ×8 — encoded in an extra byte called SIB (scale-index-base) that follows the ModRM byte when `rm = 0b100`. So `[rcx + rax*4]` indexes an int array with no separate multiply instruction. The 8086's fixed 8-entry table became fully general.

</details>

---

## 2. How to Look at Your Own Engine's Assembly

Four tools, in order of usefulness:

1. **[godbolt.org](https://godbolt.org)** (Compiler Explorer) — paste C++, pick `x64 msvc latest` (or clang), add `/O2`. Color-links each source line to its assembly. The fastest feedback loop for "what does this compile to?"
2. **Visual Studio disassembly window** — set a breakpoint in your engine, run, then right-click → *Go To Disassembly* (or `Ctrl+Alt+D`). Shows live assembly with your real data in registers. Use a **Release build with debug info**, otherwise you're reading unoptimized code that looks nothing like what ships.
3. **`cl /O2 /FA file.cpp`** — emits a `.asm` listing next to the `.obj`, annotated with your source lines. What this document was made with.
4. **`dumpbin /disasm:bytes file.obj`** — disassembly *with raw bytes*, so you can decode ModRM yourself like in the guide's section 4.

One surprise you'll hit immediately: names like `?GetHealth@@YAHPEBUPlayer@@@Z`. That's C++ **name mangling** — the signature encoded into the symbol so overloads link correctly. Read past it; the comment next to it usually has the demangled name.

**Rule of thumb for what to expect:** debug builds (`/Od`) spill everything to the stack and are misleading; `/O2` is what your players run. Always read `/O2`.

---

## 3. Struct Fields & Arrays — mod/rm's Direct Descendants

The C++ everyone writes in a game engine:

```cpp
struct Player { int health; int mana; float x, y, z; };

int   GetHealth(const Player* player) { return player->health; }
int   GetMana(const Player* player)   { return player->mana; }
float GetZ(const Player* player)      { return player->z; }
int   GetValue(const int* values, int index) { return values[index]; }
```

MSVC `/O2` output, with bytes:

```
GetHealth:
  8B 01              mov   eax, dword ptr [rcx]         ; health at offset 0
  C3                 ret

GetMana:
  8B 41 04           mov   eax, dword ptr [rcx+4]       ; mana at offset 4
  C3                 ret

GetZ:
  F3 0F 10 41 10     movss xmm0, dword ptr [rcx+16]     ; z at offset 16 (0x10)
  C3                 ret

GetValue:
  48 63 C2           movsxd rax, edx                    ; sign-extend index to 64-bit
  8B 04 81           mov   eax, dword ptr [rcx+rax*4]   ; base + index*4
  C3                 ret
```

What to see here:

- **A member access is just a displacement.** `player->mana` = "the pointer in `rcx`, plus constant 4" — precisely `mod=0b01` + disp8 from the guide's section 4.3. The compiler computed the offset from the struct layout at compile time; there is no "field lookup" at runtime.
- **`rcx` is the first argument** by Windows x64 convention (section 5) — for a member function, that's `this`.
- **Array indexing is one instruction.** `values[index]` becomes `[rcx + rax*4]` via the SIB byte: decode `8B 04 81` → ModRM `0b00|000|100` (rm=`0b100` means "SIB follows"), SIB `0b10|000|001` = scale ×4, index `rax`, base `rcx`.
- **A getter costs 3 bytes.** This is why trivial getters are free — the compiler inlines them at call sites anyway, and even un-inlined they're a single `mov`.
- **Struct layout is visible.** `z` at offset 16 = 2 ints (8) + 2 floats (8). Reorder your members and these displacements change — this is where cache-friendly layout decisions become concrete.

<details>
<summary><strong>Q: You reorder Player so z comes first. What changes in GetZ's assembly, and what does NOT change?</strong></summary>

The displacement changes: `movss xmm0, dword ptr [rcx+16]` becomes `movss xmm0, dword ptr [rcx]` (offset 0, and the instruction gets 1 byte shorter since mod goes from `0b01` to `0b00`). Nothing else changes — same instruction count, same cost. Field access is O(1) constant-offset regardless of position; layout matters for *cache lines* (which fields travel together), not for access instruction count.

</details>

<details>
<summary><strong>Q: Why does GetValue need the movsxd instruction before the load?</strong></summary>

The index arrives as a 32-bit `int` in `edx`, but address arithmetic is 64-bit — `movsxd rax, edx` sign-extends it to 64 bits first (the same widening idea as `s=1` immediates in the guide's section 4.5, applied to a register). This instruction is pure overhead from using `int` as an index type; using `size_t`/`ptrdiff_t` indexes often lets the compiler drop it.

</details>

<details>
<summary><strong>Q: In 8B 04 81 (mov eax, [rcx+rax*4]), decode the SIB byte 0b10000001 (0x81) into its three fields.</strong></summary>

SIB has the same 2-3-3 split as ModRM: `0b10|000|001` → **scale** = `0b10` (×4), **index** = `0b000` (rax), **base** = `0b001` (rcx). Address = `rcx + rax*4 + 0`. The ×4 exists so array indexing by element (not byte) needs no separate shift/multiply.

</details>

---

## 4. Loops, Branches, Flags — cmp + jnz Forever

```cpp
int CountUntilZero(const int* values)
{
    int count = 0;
    while (values[count] != 0) ++count;
    return count;
}
```

```
  33 C0              xor   eax, eax                     ; count = 0
  39 01              cmp   dword ptr [rcx], eax         ; first element zero?
  74 15              je    ret_label                    ; yes → return 0
loop:
  FF C0              inc   eax                          ; ++count
  48 63 D0           movsxd rdx, eax
  83 3C 91 00        cmp   dword ptr [rcx+rdx*4], 0     ; values[count] != 0 ?
  75 F5              jne   loop                         ; not zero → keep going
ret_label:
  C3                 ret
```

This is sections 5 and 7 of the sim8086 guide, unchanged:

- `83 3C 91 00` starts with `0b10000011 (0x83)` — the **same arith-imm opcode with s=1, w=1** you decoded in the guide's section 4.5, still using the reg slot of ModRM as the operation extension (`0b111` = cmp, same table as `getArithmeticMnemonic`), still sign-extending a 1-byte immediate.
- `75 F5` is the **same `0b01110101` jnz/jne byte** from the guide's section 5.1, with the same target math: `0xF5` = −11, target = address-after-jump − 11 → back to `loop`.
- `xor eax, eax` instead of `mov eax, 0` is a classic idiom: 2 bytes instead of 5, and CPUs special-case it as "zero this register" with no real execution cost.
- Note what the compiler did to your `while`: it **rotated the loop** — the condition check for the first iteration is peeled off before the loop, so the loop body ends with a single `cmp`+`jne` (one branch per iteration instead of two).

An `if` statement is the same machinery without the backward jump: `cmp` (or `test`) to set flags, then a forward `jcc` over the skipped block. Every branch in your engine — `if`, `while`, `for`, `&&`, `switch` — reduces to this flags-then-conditional-jump pattern you simulated in `ExecuteInstruction`.

<details>
<summary><strong>Q: The loop's jne has displacement byte 0b11110101 (0xF5) and sits so the next instruction is at address 0x1B. Where does it jump? Use the guide's section 5.2 formula.</strong></summary>

`0xF5` as `int8_t` = −11. Target = (address after the jump) + displacement = `0x1B − 11 = 0x10` — the `inc eax` at the top of the loop. Same formula as `DecodeJump`: relative to the instruction *after* the jump, sign-extended. Nothing changed since the 8086 except address width.

</details>

<details>
<summary><strong>Q: Why does the optimizer check values[0] separately before entering the loop instead of compiling the while-loop check directly?</strong></summary>

Loop rotation (a.k.a. loop inversion). A literal `while` translation needs a check-at-top plus an unconditional jump-back — **two** branches per iteration. Rotating it (peel the first check, put the condition at the *bottom*) leaves one `cmp`+`jne` per iteration. The peeled entry check runs once. Fewer branches per iteration = the standard shape you'll see for nearly every loop in optimized engine code.

</details>

<details>
<summary><strong>Q: Where in this x64 loop are the zero flag and sign flag from the sim8086 simulator?</strong></summary>

Same place, same job: `cmp dword ptr [rcx+rdx*4], 0` performs the subtraction and sets ZF (and SF, CF, OF, PF) exactly like the `cmp` branch of `ExecuteInstruction` sets `zeroFlag`/`signFlag`; `jne` then tests ZF exactly like the simulator's `if (!zeroFlag)`. The simulator's two `bool`s are a 2-flag subset of the real FLAGS register.

</details>

---

## 5. Function Calls & Virtual Dispatch

**Windows x64 calling convention** (the contract every function in your engine follows):

| What | Where |
|---|---|
| Integer/pointer args 1–4 | `rcx`, `rdx`, `r8`, `r9` (5th+ on the stack) |
| Float/double args 1–4 | `xmm0`–`xmm3` |
| Integer/pointer return | `rax` |
| Float return | `xmm0` |
| Struct return > 8 bytes | Caller passes a hidden pointer as arg 1 (`rcx`); callee fills it and returns the pointer in `rax` |
| `this` | First argument → `rcx` |
| Must survive a call ("non-volatile") | `rbx`, `rbp`, `rsi`, `rdi`, `r12`–`r15`, `xmm6`–`xmm15` |

That's why every example above reads its first argument from `rcx`. Two consequences visible in real output:

- **Leaf functions have no prologue.** `GetHealth` is 3 bytes total — no stack frame, nothing saved, because it calls nothing and only touches volatile registers. The 8086 homework never needed a stack; simple x64 functions don't either.
- **Using a non-volatile register costs setup.** `SumArray` (section 7) wants `rbx` mid-function, so it must save and restore it: `mov [rsp], rbx` … `mov rbx, [rsp]` — that's most of what prologues/epilogues are.

**Virtual dispatch** — the thing every C++ engine discussion eventually reaches:

```cpp
struct Enemy { virtual int Damage() const; };
int GetDamage(const Enemy* enemy) { return enemy->Damage(); }
```

```
  48 8B 01           mov rax, qword ptr [rcx]    ; load vtable pointer (hidden first member)
  48 FF 20           jmp qword ptr [rax]         ; jump through vtable slot 0
```

Two dependent memory loads, then an **indirect** jump:

1. Every polymorphic object secretly starts with a pointer to its class's **vtable** (why `sizeof` grows by 8 when you add your first `virtual`).
2. `[rcx]` loads that vtable pointer; `[rax]` loads the function address from slot 0 (`Damage` is the first virtual). A second virtual would be `jmp [rax+8]`.
3. It's `jmp`, not `call` — MSVC noticed `GetDamage` does nothing afterward, so it **tail-calls**: `Damage` returns straight to `GetDamage`'s caller. (Note `0x48 0xFF /4` — the ModRM reg field selects *which* instruction `0xFF` is, the same opcode-extension trick as `0b1100011w` mov in the guide's section 4.4.)

The cost isn't the jump itself — it's that the target is **data-dependent** (unknowable until both loads finish, hostile to branch prediction with mixed types) and **uninlinable** (the optimizer can't see through it, so no constant folding, no vectorization across the call). Calling a virtual per-entity per-frame across a heterogeneous array is the canonical slow path; sorting by type or using non-virtual data-oriented loops is the canonical fix. Now you can see *why* in the bytes.

<details>
<summary><strong>Q: You call obj->Update() where Update is the 3rd virtual function declared in the class. What does the dispatch assembly look like?</strong></summary>

```
mov rax, qword ptr [rcx]      ; vtable pointer
call qword ptr [rax+16]       ; slot 2 → offset 2*8 = 16
```

Vtable slots are laid out in declaration order, 8 bytes each on x64 — so the 3rd virtual lives at `[vtable + 16]`. The slot offset is fixed at compile time; only the vtable *pointer* varies per object. (A `jmp` instead of `call` only when it's a tail call.)

</details>

<details>
<summary><strong>Q: Why is a virtual call fundamentally more expensive than the 3-byte GetHealth, beyond just executing 2 instructions instead of 1?</strong></summary>

Three compounding reasons: (1) the two loads are **dependent** — the second can't start until the first finishes, and if the object isn't in cache that's two serialized memory latencies; (2) the branch target is **data-driven**, so with mixed concrete types the CPU's indirect-branch predictor misses; (3) it's an **optimization barrier** — the compiler can't inline what it can't identify, so every optimization that needs to see across the call boundary (constant propagation, vectorization, dead-code elimination) is off. GetHealth, by contrast, typically doesn't survive as a call at all — it's inlined into a single `mov` at each call site.

</details>

<details>
<summary><strong>Q: Where does the machine code for GetDamage check what type enemy points to?</strong></summary>

It doesn't — nowhere. There is no type check, no lookup by name, no metadata scan. The "polymorphism" is entirely the data: whatever vtable pointer sits in the object's first 8 bytes decides where the `jmp` lands. Construction wrote that pointer once; dispatch just follows it. That's the whole mechanism — and also why corrupting the first 8 bytes of a polymorphic object produces a jump to garbage.

</details>

---

## 6. Float Math & SIMD — the Vec3 Everyone Writes

```cpp
struct Vec3 { float x, y, z; };
Vec3 Add(const Vec3& a, const Vec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
```

```
  F3 0F 10 02        movss xmm0, dword ptr [rdx]      ; load a.x
  48 8B C1           mov   rax, rcx                   ; rax = return slot (see below)
  F3 41 0F 58 00     addss xmm0, dword ptr [r8]       ; a.x + b.x          (1 float)
  F2 41 0F 10 48 04  movsd xmm1, mmword ptr [r8+4]    ; load b.y AND b.z   (8 bytes)
  F3 0F 11 01        movss dword ptr [rcx], xmm0      ; store result.x
  F2 0F 10 42 04     movsd xmm0, mmword ptr [rdx+4]   ; load a.y AND a.z
  0F 58 C1           addps xmm0, xmm1                 ; y+y AND z+z        (packed!)
  F2 0F 11 41 04     movsd mmword ptr [rcx+4], xmm0   ; store result.y and .z
  C3                 ret
```

What to see here:

- **Floats live in `xmm` registers**, not the integer registers — a separate 128-bit register file (SSE). All float math in your engine goes through these.
- **`addss` vs `addps`** — the suffix is the story: `ss` = *scalar single* (1 float), `ps` = *packed single* (4 floats at once). The compiler did `x` scalar, then noticed `y` and `z` are adjacent in memory and did them **together** with one packed add. Nobody asked for SIMD; contiguous layout made it possible.
- **The hidden return pointer in action** (section 5): `Vec3` is 12 bytes > 8, so the caller passed a result slot in `rcx`; `a` landed in `rdx`, `b` in `r8`. The `mov rax, rcx` is the "return the pointer" part of the contract.
- In real engine code this function would be **inlined** and the loads/stores fused into the surrounding math — what you see here is the worst case (a standalone call).

This is also the mechanical argument for **SoA (structure of arrays)** hot paths: `addps` can add 4 floats per instruction (8 with AVX `ymm`), but only if the data is packed contiguously. An array of `Vec3` (AoS) gives you the awkward 1+2 split above; separate `x[]`, `y[]`, `z[]` arrays give the compiler clean 4-wide packs.

---

## 7. What /O2 Really Does — Your Loop, Auto-Vectorized

The naive sum every engine has somewhere:

```cpp
int SumArray(const int* values, int count)
{
    int sum = 0;
    for (int i = 0; i < count; ++i) sum += values[i];
    return sum;
}
```

MSVC does **not** compile this loop as written. Condensed real output:

```
  test  r11d, r11d               ; count <= 0?
  jle   return_zero
  cmp   r11d, 8                  ; fewer than 8 elements?
  jb    scalar_remainder         ; → skip SIMD entirely
  and   r8d, -8                  ; r8d = count rounded down to multiple of 8
  xorps xmm2, xmm2               ; vector accumulator A = {0,0,0,0}
  xorps xmm1, xmm1               ; vector accumulator B = {0,0,0,0}
simd_loop:                       ; ---- 8 ints per iteration ----
  movdqu xmm0, [rdx+rcx*4]       ; load ints 0..3
  paddd  xmm2, xmm0              ; A += ints 0..3   (4 adds, 1 instruction)
  movdqu xmm0, [rdx+rcx*4+16]    ; load ints 4..7
  paddd  xmm1, xmm0              ; B += ints 4..7
  cmp    eax, r8d
  jl     simd_loop
                                 ; ---- horizontal reduction ----
  paddd  xmm1, xmm2              ; combine A and B
  psrldq xmm0, 8 / paddd         ; fold 4 lanes → 2
  psrldq xmm0, 4 / paddd         ; fold 2 lanes → 1
  movd   r10d, xmm1              ; vector total → scalar
scalar_remainder:                ; ---- leftover 0..7 elements ----
  ...                            ; a 2-at-a-time loop, then 1 final add
```

Read what happened to your 3-line loop:

- **The compiler rewrote the algorithm.** One accumulator became *two independent vector accumulators* (`xmm1`, `xmm2`) so the two `paddd` chains don't wait on each other — instruction-level parallelism, 8 ints per iteration.
- **`paddd` = packed add of 4 × 32-bit ints** in one instruction. The `cmp`/`jl` loop skeleton around it is still exactly section 4.
- **The guard `cmp r11d, 8 / jb`** exists because the vector path only works in blocks of 8; small arrays go straight to the scalar path. Then a *remainder* section mops up the 0–7 leftover elements. One source loop became three machine loops.
- **When does this happen?** Simple counted loop + contiguous data + no cross-iteration dependency + no function calls inside. Break any of those (an early `return` inside, a virtual call, a linked-list walk) and it drops back to scalar — compare `CountUntilZero`, which stayed scalar because each iteration's *continuation* depends on loaded data.

This is the core Computer Enhance lesson in one listing: the compiler is very good at *this shape* of code — and knowing which shapes it can and can't transform is worth more than micro-optimizing syntax.

<details>
<summary><strong>Q: In the Vec3 Add output (section 6), why does the y/z pair get addps (packed) but x gets addss (scalar)? Could the compiler have done all three in one instruction?</strong></summary>

`movsd` (here: move 8 bytes) can grab `y` and `z` together because they're adjacent, and `addps` adds lanes independently, so 2 useful lanes + 2 ignored lanes is fine. All three in one add would need a 12-byte load into a 16-byte register — reading 4 bytes past the struct, which may cross into an unmapped page. The compiler must be conservative about the tail. (Padding `Vec3` to 16 bytes, or SoA layout, removes the problem — a concrete example of layout enabling SIMD.)

</details>

<details>
<summary><strong>Q: Why does SumArray keep TWO vector accumulators (xmm1, xmm2) instead of one?</strong></summary>

Dependency breaking. With one accumulator, each `paddd` must wait for the previous `paddd`'s result — the loop runs at the *latency* of the add chain. With two, the `A += ...` and `B += ...` chains are independent and execute in parallel, roughly doubling throughput; they're merged once (`paddd xmm1, xmm2`) after the loop. Same trick applies to scalar code — and notice the compiler also did it in the *scalar remainder* (`r8d`/`r9d` pair). The sim8086 simulator models the sequential world (guide section 7); real CPUs overlap independent work, and optimizers restructure code to feed them.

</details>

<details>
<summary><strong>Q: SumArray auto-vectorized but CountUntilZero didn't. What's the structural difference?</strong></summary>

The trip count. `SumArray` runs a **known** number of iterations (`count`), independent of the data — the compiler can safely load 8 elements at a time because it knows they're all in-bounds. `CountUntilZero`'s exit depends on *the values being loaded* — reading 8 ahead might touch memory past the terminating zero that was never yours to read. Data-dependent exits (string length, linked lists, early-out searches) are the classic vectorization blockers; counted loops over arrays are the classic green light.

</details>

<details>
<summary><strong>Q: Your profiler shows a hot loop didn't vectorize. Name three things in the C++ that commonly block it.</strong></summary>

Any three of: a **data-dependent early exit** (`break`/`return` on a loaded value); a **non-inlined function call** in the body (including any virtual call); **pointer aliasing** the compiler can't disprove (e.g. writing through one pointer while reading another of the same type — `__restrict` helps); **non-contiguous access** (linked structures, big strides, AoS field-hopping); or **cross-iteration dependencies** (each iteration needs the previous one's result — though reductions like `sum +=` are handled, as SumArray shows). MSVC will tell you which: compile with `/Qvec-report:2`.

</details>

---

## 8. Where Unity Fits

Your C# doesn't go straight to x64 — there's a pipeline, and where it lands determines what assembly you get:

```
C# ──(Unity/Roslyn compiler)──► IL (.NET bytecode, in your .dll)
      │
      ├─ Mono:    IL ──(JIT at runtime)──► native        (editor, Mono builds)
      ├─ IL2CPP:  IL ──► generated C++ ──(MSVC/clang)──► native   (iOS, consoles, opt-in elsewhere)
      └─ Burst:   HPC# jobs ──(LLVM)──► aggressively SIMD-vectorized native
```

- **Inspecting each stage:** IL with **ILSpy/dnSpy** (open your `Assembly-CSharp.dll`); JIT x64 for plain C# with **[sharplab.io](https://sharplab.io)** (Results → JIT Asm) or godbolt's C# mode; Burst output in-editor via **Jobs → Burst → Open Inspector**, which shows the final x64/ARM assembly of each Burst-compiled job — read with exactly the skills from this guide.
- **What managed code adds** — visible in JIT output: every `array[i]` gets a **bounds check** (`cmp` index vs length + `jae` to a throw helper — a real extra branch in the hot path, usually removed inside `for (i = 0; i < array.Length; i++)` loops where the JIT can prove safety), reference-type field access goes through GC-managed pointers, and allocations carry GC bookkeeping.
- **Why Burst code looks so different:** the HPC# subset (no classes, no GC, `NativeArray`/structs only) removes exactly the features that block optimization — so Burst's LLVM backend can vectorize like the `SumArray` example, and typically more aggressively than MSVC. The "data-oriented, contiguous, no-indirection" style sections 5–7 argue for is *mandatory* there, which is precisely why it's fast.
- Same mental model as C++ in the end: structs of value types → base + displacement; `NativeArray<T>` indexing → base + index·scale; jobs' hot loops → `cmp`/`jcc` skeletons around packed math.

<details>
<summary><strong>Q: In JIT assembly for C# array access you see cmp edx, dword ptr [rcx+8] followed by jae to a helper. What is this, and why [rcx+8]?</strong></summary>

The **array bounds check**. A .NET array object stores its length in a header field — with the method-table pointer at offset 0, length sits at offset 8 on x64, so `[rcx+8]` is `array.Length` (base + displacement, same as any struct field). `jae` (jump if above or equal, *unsigned*) branches to the IndexOutOfRange throw helper — the unsigned compare cleverly catches negative indexes too, since −1 as unsigned is huge. In `for (int i = 0; i < a.Length; i++)` loops the JIT proves the index safe and drops the check.

</details>

<details>
<summary><strong>Q: Why can Burst often vectorize code that the regular Mono/IL2CPP path can't?</strong></summary>

Because HPC# removes the blockers *by construction*: no reference types or GC (no barriers, no aliased managed pointers), data in `NativeArray`/blittable structs (guaranteed contiguous, known layout), no virtual dispatch, and the job system's safety rules give the compiler strong no-aliasing guarantees between containers. That leaves exactly the "counted loop over contiguous independent data" shape that section 7 showed vectorizes cleanly — handed to LLVM with safety checks off in builds.

</details>

---

## 9. A Practice Workflow for Building Intuition

The skill you built decoding 8086 by hand becomes *fluency* through a predict-then-check loop:

1. **Pick one small, real function** from your engine — a getter, a math routine, one hot loop. Not a whole system.
2. **Predict before looking:** How many memory reads? Any call instructions left, or does it all inline? Will the loop vectorize (checklist from section 7)? Where are the branches?
3. **Look** — godbolt for experiments, `/FA` or the VS disassembly window for your actual build flags. Score your prediction.
4. **Poke it:** add a `virtual`, reorder struct fields, swap `int` index for `size_t`, add an early-`break`, replace a raw loop with `std::accumulate` — and watch what the output does. Godbolt's diff view makes changes jump out.
5. **Trust the loop skeletons.** Whatever the body does, find the `cmp`/`jcc` pairs first — they're the map of the control flow, and you already know how to read them from the guide's section 5.

You don't need to read every instruction — engine-scale fluency is recognizing the handful of patterns from this document (displacement = field, SIB = array, dependent-load-pair + indirect jump = virtual, `ps`-suffix blocks = SIMD, guard + main + remainder = vectorized loop) and noticing when something *doesn't* match your prediction. That mismatch is where the performance work lives — which is exactly where the Computer Enhance course goes next.

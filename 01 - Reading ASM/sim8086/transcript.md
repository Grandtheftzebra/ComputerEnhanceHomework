This is the second video in Part 1 of the Performance-Aware Programming series. Please see the Table of Contents to quickly navigate through the rest of the course as it is updated weekly. Homework data is available on the github. A lightly-edited transcript of the video appears below.

In the previous video I gave you some homework where I asked you to decode a register-to-register move instruction on the original 8086. I hope it wasn't too difficult! I tried to pick something simple for the first assignment, but at the end of the day, nothing is ever really that simple in the land of 8086. Even a simple instruction is kind of tricky to decode, as you may have noticed!

I hope it went okay, but if you struggled with it, that's perfectly fine! At the end of these instruction decode videos, I will go over the code that I wrote as a reference, and you can see potential solutions to any problems you may have had. For now, what I'd like to do is add a little bit more to the problem. I'd like to give you a chance to work out a few more details of 8086 instruction decode, just to get you in the mindset of a CPU and what it really does.

Last time we talked about the two-byte encoding for register-to-register move:



In this encoding, we relied on the MOD field to tell us it was a register-to-register move — when the MOD field was 11, that meant register-to-register.

But in the homework assignment, you could have just skipped that check because I was only asking you asking you to decode this specific kind of instruction. You didn't really have to check the MOD field, and honestly you didn’t really even have to check the 100010 opcode because every instruction was going to have those two things set the same way.

What I'd like to do now is expand the decoding so that you do have to check everything. This is just to really make sure you’ve got it all down, and also so you can see how the actual decoding process changes dramatically from instruction to instruction even when you're looking at the same assembly language mnemonic. Even if we just stick with MOV, as you’ll see, the assembler may generate radically different instruction encodings depending on what is being copied where.

You already saw a preview of this in the 8086 manual, where it listed several possible encodings under MOV in the encoding table. But even with just the instruction we already decoded — register-to-register move — we only decoded a subset of that particular encoding because when the MOD field is not 11, that exact same encoding might indicate a move to or from memory.

So the first thing I’d like us to do is expand our handling of that one instruction encoding to decode all the possible things it can encode. This means handling the other possible values of the MOD field, and also parsing the optional bytes that we say in that table, the ones labeled “DISP-LO” and “DISP-HI”:



The other values of MOD are there to encode memory-to-register moves (loads) and register-to-memory moves (stores). And some combinations of values will require parsing the two optional DISP bytes.

Before I go any further, I really want to make sure you understand how significant what I’m saying is in terms of how a CPU has to process these instructions in the 8086 lineage1: if the mod field is 11, we don't have these optional bytes. If the mod field is 01, we have one extra byte, and if it's 10, we have two extra bytes. So not only does the CPU have to look at the first byte of this instruction, check the opcode, and then decide to read a second byte for the MOD REG R/M fields, but then it also has to check the second byte to see what the MOD field (and potentially R/M, as we’ll see) to find out whether to read a third and fourth byte!

It really is a nasty, dependency-chain process that the CPU has to do to decode these instructions. This actually causes a lot of headaches in modern CPU design, as you might imagine, because if a CPU needs to decode four, five, six instructions per clock, well, how does it do that if it has to decode the first one to know where the second starts? It’s a much harder problem than in an instruction set where the size of instructions is fixed.

But thankfully, that’s not our problem, that’s Intel and AMD’s problem! But you’re quickly gaining some insight into the kind of stuff their chip designers have to contend with.

Anyway, back to memory moves. First of all, how do we notate a memory-to-register or register-to-memory move in assembly language? One way to write it is if we want to do a load, we begin with something like “mov ax,” like we did before, but instead of a second register for a source, we use brackets to enclose a memory address to read from:



The memory address can be as simple as just a constant, like 75. That would mean we're going to load the byte from address 75 and move it into the ax register — and since ax is a 16-bit register, it’ll actually move byte 76 into the high bits of ax, too.

Now, because I am a bit sneaky, I picked “ax” here because I wanted to foreshadow a challenge problem in the homework. If you actually tried to assemble this as-is, you might be shocked to find that many assemblers will not produce the memory-to-register MOV encoding! What has happened? Well, if that piques your interest, the challenge problems in the homework are for you.

I’ll leave that little teaser there for now, and just slyly change ax to bx and continue on:



So if we wrote something like this, how is this going to be encoded? This is saying we will load 16 bits into bx from memory address 75 (and for the high byte, 76). It’s a 16-bit move, so we know that the W bit will be set to 1, just like with the register-to-register move.

At this point, I should mention that if you are coming from a language that doesn't have pointers, and you're uncomfortable with how memory addresses work or things like that, this memory move instruction might be confusing to you. But it doesn't really have to be for purposes of understanding the 8086, or honestly even for understanding pointers at all. Pointers and memory are actually way simpler than people make them out to be!

If you imagine you just had an array of bytes called Memory, and it was 65536 bytes lone — 64k worth of bytes — that is exactly what we the CPU is thinking of when you do a memory access on 8086. The [75] is jus like looking up index 75 in an array of bytes, and getting out the value. That’s all the CPU is doing. A “memory address” or a “pointer” is nothing more than the index in the array of memory where you are going to look for some data. There’s seriously nothing more to it than that!

In this case, because we’re reading 16 bits, we’re just accessing two elements of the array — [75], which goes into the low 8 bits of bx, and [76], which goes into the high 8 bits. That’s it.

So that’s how we write a load — a copy from memory into a register — in 8086 assembly. We could also do a store — a copy from a register back to memory. We write it the exact same way, but with the operands reversed:



How do we encode these loads and stores in the instruction stream? We use the MOD and R/M fields.

Unlike register-to-register moves, where MOD was 11, when a move involves memory the MOD field will be 00, 01, or 10. That’s our first step in decoding the memory address: if the MOD field is not 11, then we know we have to decode something more complicated than just a register name.

The effective address calculation is what we call resolving the address specified by the expression within the brackets. When it’s something as simple as 75, it doesn’t look like an expression, but it is. And we can use a more complex expression if we want:



For example, we can use [bp + 75], which says to take the value of the bp register and add 75 to it.

But our options aren’t unlimited. We have to choose from a few preset expressions. For example, we could not write [2ax + bx + 7]:



That’s not one of the possible patterns.

Why are there only certain patterns? The answer becomes obvious once we look at how these expressions are encoded. They are derived directly from the two bits in MOD and the three bits in R/M — which, as the small number of total bits would indicate, can only encode a small number of expression patterns.

First, the MOD field specifies if there is a displacement. In the expression [bp + 75], the constant 75 is the displacement. If there is no displacement, MOD is 00. If there is an 8-bit displacement, MOD is 01, and that “DISP-LO” byte we talked about earlier comes after the instruction. And as you might guess, is MOD is 10, there’s a 16-bit displacement, and both the “DISP-LO” and “DISP-HI” bytes will follow the instruction.

So that’s the first part of the decoding: check the MOD field, and read the extra bytes if they’re there. But there’s a catch — if you want to do the challenge homework, you’ll need to watch out for a special case with MOD 00! More on that in a second.

After looking at the MOD field, we need to look at the R/M field to figure out what expression pattern we will use. It’s another table which maps the 3 bits in R/M to eight different expressions:



Those eight expressions are the only expressions we get! They are assorted combinations of bx, si, bp, and di — no other register can appear!

So that’s all we get. We get those eight expressions, and then using the MOD field we can say whether we want to add an 8-bit or 16-bit constant to the end. Now for the catch!

The expression pattern when R/M is 110 is usually just [bp]. If MOD is 01, it will be [bp + 8-bit constant], and if MOD is 10, it will be [bp + 16-bit constant]. But if MOD is 00, instead of the expression being [bp] — which is what you would expect — it changes to being just a 16-bit constant. This is called direct addressing, and it’s actually how we would encode the bare [75] expression I showed earlier! It’s actually an oddity of the table.

So that’s how you encode the address you’re reading from. And you’ll notice I said reading from. What about writing to? Do we have to do something special to encode a write instead of a read?

Thankfully, we don’t. Remember the D field? Well this is why that field exists in the first place! If the D field is set, then we know the REG parameter encodes the destination register. So it will be set when we are doing a read from memory. But if the D field is zero, that means REG encodes the source register, right? So that’s how we know which way the mov goes! The D field tells us whether the memory expression — which is always decoded from MOD and R/M — is the first operand or the second operand in our ASM.

Now you may have noticed, when you did the homework last time, that there are more than just the ax, bx, cx, and dx registers. Because if you did the homework, you had to decode the REG field, and that field encoded some more registers — some of which we’ve used in the effective address table.

And you may also have noticed that ax, bx, cx, and dx all had l and h variants, so you could specifically talk about the high bits or low bits of those registers. But there were no such variants for the other four registers in the table, sp, bp, si, and di. Why is this?

Well, again, x86 has a reputation for being a crazy instruction set for a reason, and this is just one of those reasons. Even with just these eight registers, you can already see significant limitations on what you can and can’t do with each type of register. We’ve only looked at one instruction so far and we’ve already seen two major differences!

The first major difference is that some registers can have their low and high bytes targeted separately: ax, bx, cx, and dx. The second major difference is that some registers can appear in memory address expressions: bx, bp, si, di. Can you see why this instruction set would be hard to program well? Or to write a good compiler for? Think of all the constraints you have to consider when trying to write the instruction streams!

From our perspective, since we’re trying to learn about CPUs, it’s actually kind of fun. If you’re like me, you like seeing this kind of complexity and learning about it. So from the perspective of a fun homework problem, this is kinda cool! But if you think about how this must have been when it was your job to ship reliable, efficient code generators for this ISA… yikes.

OK, so that’s the first part of the homework. The second part of the homework is decoding a second instruction opcode.

Thus far, we’ve only talked about decoding one opcode, which is 100010. You didn’t really even have to check it for the previous homework, because all the instructions had that pattern in the top bits of the first byte.

Now, I’d like to add a second opcode, 1011. This is the opcode for an immediate-to-register move.

We haven’t really talked about immediates yet. It’s a somewhat weird term, but it’s a very simple concept: an immediate is a data value that is encoded directly into the instruction stream, so that it can be fetched when the instruction stream is fetched rather than requiring a separate move instruction to read.

In a way, the displacement value we just saw was an “immediate”. It was a constant value we were using to offset a memory access, and instead of it coming from a register, it came directly from the instruction stream.

The main reason for these immediates is, as the name implies, to be more efficient. Since the instruction stream is being fetched in order, if we know we’re always going to need some constant value for an operation, we can simply encode it directly into the stream and save the CPU the trouble of decoding an entire instruction just to read the constant separately.

Moving an immediate to a register has a similar syntax to all the other moves. If we want to move the value 12 into the register ax, we just write “mov ax, 12”:



How is this encoded?

As I said before, the opcode for this kind of mov is 1011. So the high bits of the first instruction byte will be 1011. Following that, we will have the same W field we had before: if it’s a 16-bit register, it will be 1, if it’s 8-bit, 0. Note that the meaning of this bit is the same, but it’s location is different! It used to be the bottom bit, now it’s the fourth bit, so watch out for that.

But that’s not the only thing the moved. The REG field, which encodes the register name, is now in the first byte. It used to be in the second byte! Why all this shuffling around? Well, remember, memory was expensive back then, and fetching bytes from it was slow. So common instructions like this one were made as compact as possible! If a byte could be saved, it was.

So this instruction takes up only one byte of control information, unlike the previous instruction which took two. But it does have more bytes, of course: if W is 0, then there will be a second byte to encode the 8-bit value to move into the register. If W is 1, then there will also be a third byte to encode the 8-bit value to move into the high bits of a 16-bit register.

That’s everything for today’s homework. It gets really complicated really fast, doesn’t it? Can you see why I wanted you to get a feel for how much intricacy there is in x86 instruction decoding? With just two opcodes, we’ve already seen all sorts of different bits packed in all sorts of different ways! And our instructions can be one, two, three, or four bytes long.

Before we take a look on the computer and go over the listings, I did want to mention one more thing if you’re planning on doing the challenge homework. Yes, there is challenge homework this time because… well, why not? It’s optional, don’t worry.

In assembly language there is an extra syntactic element you use when the assembler wouldn’t be able to automatically tell what the W bit should be in the encoded instruction. If you are going to do something like move an immediate directly to memory, without going through register, then there wouldn’t be any “ax” or “al” or “ah” to tell the assembler whether to use 8 bits or 16 bits. In this case, you must add the “byte” or “word” keyword to the immediate value to tell the assembler whether it should be one byte or two:



So, “byte 12” means a 12 encoded in 8 bits, whereas “word 12” means a 12 encoded in 16 bits. Obviously all the top bits are 0 in both cases, but it changes how much memory is written. In the byte case, it’s one byte, and in the word case, it’s two.

In a way, it's almost like a cast — it's telling the assembler exactly what size something should be, so it doesn’t have to guess.

Okay, let's go over to the computer now and I'll walk you through the homework assignment and the parts of the 8086 manual you’ll need. You'll probably remember most of this from last time, but I just want to refresh your memory.

Here is listing thirty-nine, which is the new homework assignment:

; ========================================================================
; LISTING 39
; ========================================================================

bits 16

; Register-to-register
mov si, bx
mov dh, al

; 8-bit immediate-to-register
mov cl, 12
mov ch, -12

; 16-bit immediate-to-register
mov cx, 12
mov cx, -12
mov dx, 3948
mov dx, -3948

; Source address calculation
mov al, [bx + si]
mov bx, [bp + di]
mov dx, [bp]

; Source address calculation plus 8-bit displacement
mov ah, [bx + si + 4]

; Source address calculation plus 16-bit displacement
mov al, [bx + si + 4999]

; Dest address calculation
mov [bx + di], cx
mov [bp + si], cl
mov [bp], ch
And you can see that this time I've put in comments to label what the different lines are testing. Unlike last time, where everything was the same kind of mov, now we're testing different forms of the instruction so I wanted to make sure you could tell which was which.

If we look back at the Intel manual, you may recall table 4-12 where we found the instruction encodings for all the mov instructions:



That top instruction, 100010, is the one we decoded last time. And this time, we need to decode more of it… but we also have to decode the third one, “immediate to register”, which is 1011. So you have to handle both those encodings, not just one… and if you plan on doing the challenge homework, well, you’ll have to handle even more than that! But I don’t want to spoil the fun.

If we look on the previous page, we can see the tables for decoding MOD and R/M into an effective address calculation. Table 4-8 explains the values of MOD:



Table 4-10 explains the values of R/M, and how they combine with MOD to form the full expression:



You can see the “special case” I mentioned there in table 4-10 under the “MOD = 00” heading. See that “DIRECT ADDRESS” entry? That’s where [BP] would have gone, as you can see from the “MOD = 01” and “MOD = 10” columns. But it’s the slot they chose for the direct memory address encoding, so that’s what MOD=11 R/M=110 decodes to instead.

If we go back to the listing, I'd like to point out something to watch out for: if the assembly language programmer put in a signed immediate, like the -12 in the listing, how are you supposed to know that? Remember, there were no bits in the encoding to say whether something was signed or not! Why not?

Well, it’s because CPUs simply don’t care about whether something is signed or unsigned until they actually do sign-sensitive operations on them. So CPUs don’t think of registers as signed or unsigned in the first place. They just store bits, and if you want them to interpret those bits as a signed value, well, they’ll do that when you tell them to do a signed instruction.

So when you move an immediate, there is actually no difference between a signed value like -12 and its unsigned counterpart, 65524. They’re the same bits to the CPU, and they’ll look exactly the same in the instruction stream.

When you disassemble this machine code, then, don’t worry if you input -12 but you get back 65524! That’s exactly what my reference disassembler does too:



How do we know this is correct? We use the trick I showed last time! If we just reassemble the disassembly, we can compare the binaries and be sure that we produced a valid disassembly indistinguishable from the original:



So that's it. That's all you have to do for homework. But for those of you who want more of a challenge, I am totally happy to provide such a challenge.

Listing forty is much harder than listing thirty-nine:

; ========================================================================
; LISTING 40
; ========================================================================

bits 16

; Signed displacements
mov ax, [bx + di - 37]
mov [si - 300], cx
mov dx, [bx - 32]

; Explicit sizes
mov [bp + di], byte 7
mov [di + 901], word 347

; Direct address
mov bp, [5]
mov bx, [3458]

; Memory-to-accumulator test
mov ax, [2555]
mov ax, [16]

; Accumulator-to-memory test
mov [2554], ax
mov [15], ax
The signed displacements listed here are the exact same instructions that appear in listing thirty-nine, but they include displacements are negative. To understand how to get that right in both the 8-bit and the 16-bit cases, you’ll have to go read the “Machine Instruction Encoding and Decoding” section of the 8086 manual carefully. Sign extension for displacements is specifically specified in the text, and you have to obey that specification.

I also added more types of mov instructions to this listing. There are explicit size tests, which actually tests two things: first, it tests whether your disassembly properly adds the “byte” and “word” keywords when necessary. Second, it tests whether you can decode this additional opcode for mov, since it is not the same opcode as the two we discussed on the lightboard!

This listing also contains those direct address moves I warned you about. These are the ones where MOD is 00 but RM is 110. If you didn’t handle that special case, you won’t be able to correctly disassemble this listing.

Finally, we have two special opcodes that actually don’t do anything you couldn’t already do with opcode 100010. These are memory reads and writes, but they can be encoded more compactly specifically when the register is ax.

Why do these instruction exists if they are redundant? Well, again, space was precious. A smaller instruction stream took up less of the expensive memory to store, and it took less of the expensive cycles to fetch, so instruction sets of this era were always looking for ways to be more efficient. This is just one way 8086 chose to try to be more compact.

So that's the challenge homework. You only have to do listing thirty-nine — that will be plenty! The point of this course is not to learn how to disassemble 8086 assembly, so once you’ve got the gist of it, that’s good enough. As long as you understand how the process works in general, it is not necessary to know all the specifics.

However, it seemed like people were enjoying the homework, so, I wanted to make sure that people who wanted to play around with 8086 disassembly could keep on playing with it! That’s what listing forty is for, so if you’re enjoying these little exercises, that one is for you.

So that's it! I hope everyone enjoys the homework, and I'll be back here next time to discuss a few more things we should learn to decode.

1
And even though it has gone through multiple major revisions, it still retains this property where it is impossible to know how many bytes an instruction requires until you’ve read and tested potentially multiple bit sequences!
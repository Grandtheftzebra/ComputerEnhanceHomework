This is the third video in Part 1 of the Performance-Aware Programming series. Please see the Table of Contents to quickly navigate through the rest of the course as it is updated weekly. Homework data is available on the github. A lightly-edited transcript of the video appears below.

If you've done the homework so far, you probably “get” 8086 encoding now. Sure, there are a lot of instructions we haven’t decoded. But as voluminous as the instruction set is, it’s all built out of the same tricks. At the end of the day, you're looking at some bits and making decisions about whether to read another byte, while extracting useful portions of each byte to use for table lookups and values. There’s really not that much to any individual bit pattern, there’s just a lot of different patterns to handle.

But you now know the basic principles. And these same principles are still there in the modern x64 instruction set. Decoders for x64 machine code do the same sort of bit pattern analysis you’re doing to decode 8086 machine code. The patterns have evolved since the original, but now that you know how to decode 8086, you would instantly recognize x64 encodings.

What I'd like to do in this post is emphasize one last aspect of 8086 decoding. It's not only an interesting pattern in the way instructions are encoded, but it’s also a design decision intrinsic to all x86-derived processors that’s important to understand for performance analysis!

If you remember, one of the encodings for a mov instruction was some opcode bits, a d bit, a w bit, and then the “mod reg r/m” pattern:




This was the encoding for doing register/register and register/memory moves.

Then there was another encoding that looked similar, but it was slightly different. It still had an opcode, and a w bit, and almost a “mod reg r/m” byte, but instead of the “reg” part, it just had three zero bits:




Both of these could have a displacement after it (depending on the mod and r/m fields), but this one also had an immediate after it - one byte if the w bit wasn’t set, two if it was:




You remember these patterns, right? Well, they appear in tons of instructions in 8086!

Recognizing these patterns is important not only because it helps you write a more concise decoder, but also because it’s a crucial aspect of how we analyze x86 CPUs for performance.

So I really want to emphasize it because it's a unique and interesting feature for the x86 and x64 instructions sets. The reason it impacts performance is because in many circumstances this pattern allows you to combine arithmetic operations with memory accesses.

Consider a situation where I use two move instructions in preparation for an addition that I want to do:




I move 16 bits from memory into ax, then another 16 bits into bx, then I add them together using the add instruction1.

Now we haven’t gotten to the part of the course where we look at how the CPU actually does these instructions. That’s the next part. But even having done only the decoding homework, you can probably read this pretty easily, right? You can see what’s going on here. It’s starting to make sense!

So I need to do these two mov instructions because I need to have two registers full of data in order to have anything to add. And if I actually want to keep the bx value around - like if I’m going to use it for something else later - this seems fine.

But what if I wasn’t going to use that value again? What if the only reason I was loading bx was to do this add? In that case, I can exploit the “mod reg r/m” pattern! It turns out add - like almost all the arithmetic operations in 8086 - also has a “mod reg r/m” encoding, so it can get its source from memory just as easily as a register! So I can combine the mov and the add into a single instruction:




This has two really important benefits. The first is that I save one entire instruction! Instead of a mov and an add, I just have the add. This means the CPU has less work to do on the front end, because it doesn’t have to do the extra work of decoding a second mov.

The second benefit is that it does not overwrite any registers! Because the add just loads the source operand from memory, adds it in, and then discards it, we never have to use a register to store it. This means whatever we were storing in bx before this code can stay in bx, which could further help us streamline out code. We wouldn’t, for example, have to re-load whatever was in bx later, because it’s still there.

These benefits come directly from the fact that the encoding for add looks exactly like the encoding for mov - the only difference is the op code. So if you can decode mov, which you’ve done in the previous posts’ homework, you can decode adds.

And guess what? There's a whole bunch of instructions that follow the same pattern! Add does, sub does, cmp does — tons of things do. If you look at the instruction tables in the manual, you’ll see a ton of instruction encodings with the “mod reg r/m” pattern.

For today’s homework, I'd like you to go ahead and decode those three instructions I just mentioned: add, sub, and cmp. Each one has three different encodings (all of which exactly mirror one of the mov encodings), but you won’t have to write nine different decodes, because they’re all identical patterns. In fact, you can probably just reuse your mov code, or modify it slightly.

and comp and I'd like you to do all three versions of each of them because each of them has the, it has this version, the mod reg rm, it has this version which is the mod and then opcode rm and I'm gonna talk about that in one second.

But I'd like to tell you one more thing about them before you give it a go — both because it’s interesting, but also because it may help you write your decoder!

What you'll notice when you look at add, sub, and cmp is a pattern in this part right here, which I mentioned in passing when we looked at mov:




Remember when I told you to note the zeroes in the second byte, because they form an extension of the opcode? Well now we get to see their true meaning! Each of the operations — add, sub, and cmp (and more!) — has their own three-bit pattern that appears right here between the mod and the r/m fields. 000 is for add, 101 is for sub, and 111 is for cmp:




The actual instruction encoding for add, sub, or cmp with an immediate always starts with the same opcode prefix — 100000. To figure out which operation you’re actually doing, you have to look at the second byte, and those three bits between the mod and r/m fields will tell you which operation you’re actually doing!

But the pattern goes further than that. Once you notice this, if you go back and look at the “mod reg r/m” version for register/register and register/memory ops, you will see the exact same three-bit pattern, it’s just in a different place! Now it’s actually in the opcode prefix, in the low-order bits:




So there's effectively an octal value — a number between zero and seven — that is telling you which arithmetic operation you’re going to do. It just changes where it is in the instruction encoding depending on which form of the encoding you’re using. But although you have to change where you extract those three bits from, once you’ve extracted them, they can be interpreted the exact same way.

There's one more thing I would like you to do for homework, and this one's way easier. It’s barely decoding compared to all the work you’ve already done with moves! There is a small annoyance with it if you do want to the trick I showed where you reassemble your disassembly to test the output. But other than that, it’s very straightforward.

What I’d like you to decode in addition to add, sub, and cmp, are the conditional jump instructions, like jnz. Conditional jump instructions are super simple to decode — they’re just a constant pattern in the first byte that you can test directly, then a signed 8-bit displacement in the second byte:




That’s it. There’s a ton of them, and you can decode them all the exact same way: test the first byte, if it matches any of the condition jump opcodes, read the signed displacement from the second byte. Done.

Now, you don’t really have to know what a conditional jump is yet. That’s the sort of thing we’re going to look at in the next set of videos! For now, all you have to do is decode the instructions, and if you can do that, you’re good to go. We’ll focus on what they do soon.

Now let's go take a look on the computer and I'll show you where the resources are for doing the homework. Also, I've got a really diabolical challenge problem for you. If for some reason you really want to do some crazy programming this weekend, I've got you covered.

First up is listing forty-one, which is the regular homework:




The first half is nothing more than permutations of adds, subs, and cmps, just to make sure you’ve covered all three encodings of each.

The second half is the conditional jumps:




As I mentioned before, decoding this is easy, but getting the assembler to reassemble them is actually quite hard. So if you’re going to go that route, you have a little extra work to do.

The reason for this is because I could not figure out any syntax in NASM where it would let me specify an exact constant for the target of a conditional jump. You can’t just put a signed number after the jump mnemonic and have NASM compile it into the corresponding opcode and displacement byte. That would have been nice!

Instead, NASM seems to think you always want constant values in a jump statement to be offset one way or another. I tried prefixing it with $, I tried brackets, I tried bare numbers… I could not find anything that would make it actually just encode the number.

Now maybe you can figure out some NASM syntax that will make it work! But I couldn’t, short of just putting a direct byte output, which is cheating of course because that’s not disassembling, that’s just copying bytes to ASCII.

So as you can see, for my disassembler, I had to basically reproduce a real “label” pattern, just like you would see in regular hand-written assembly. This is where you put identifiers followed by a colon wherever you want to jump to, and then in your jump instructions, you just say the identifier you’re jumping to:




Disassembling to this format is a bunch of extra code that has nothing to do with decoding, unfortunately. You have to actually track the instruction stream in the decoder, and go back to where each jump would land, and insert a label there, all just to make sure the output can be reassembled.

I wouldn’t recommend doing this extra work. I did it, because I’m the teacher so I don’t get off the hook so easily. But anyone taking this course should feel free not to bother with that. Instead, you can just print the signed displacement, like I did in the comments next to each conditional jump. That shows you decoded the instruction properly. The fact that NASM won’t be able to reassemble it is unfortunate, but that’s NASM’s fault, not yours!

OK, all that being said, let’s look at parts of the 8086 reference manual you’re going to need for this assignment.

First, if you look at the add, sub, and cmp encodings, you can see the patterns that I was talking about. 000 is the magic three-bit code for add, 101 is sub, and 111 is cmp:










Now for the jumps, as you can see, they’re trivial:




While the jmp instruction has multiple encodings, you don’t need to do that one. We’re only doing the conditional jumps, and those are all just an explicit opcode in the first byte, then a signed displacement. Every last one of them! So you barely have to do any decoding at all for these, compared to the gyrations you were doing for things like mov!

So that's the actual homework. I also prepared this absolutely horrific challenge:




This homework is only for the most diehard completionist. I really don't recommend you do this homework, there's no point to it. It has nothing to do with software performance. It’s only relevant to you if you like trying to figure out how to compact a large set of code permutations into a small amount of actual code, which of course is the exact kind of thing that I enjoy doing… so I did it.

If you also enjoy that sort of thing, I made this listing for you. It’s a test of all the 8086 instructions, in most (but not all) of their encodings. I didn't try to be exhaustive, but I did try to have a fairly representative cross-section.

There's also some TODOs down at the bottom:




I could not figure out, for example, how to get NASM to accept an esc instruction. I could force it to emit those bits by hand-coding the bits as bits, but I couldn't find any way to actually have it use the mnemonic. In fact, I couldn't even find it in the NASM source code. So maybe it just isn’t implemented? I don’t know.

And there’s some other TODOs like that. If anyone happens to figure out how to actually include those mnemonics in NASM, please let me know! I would love to reenable them.

So that's it. If you really want a diabolical challenge, there it is. But I really would encourage people not to do the challenge problem if you're taking this course for the stated purpose, which is learning about software performance. This has nothing to do with that. It's just kind of a side thing I made because people were having fun with the decoder, and I was having fun with the decoder, too!

Next time, we’ll go over how I did my decoder, just in case anyone wants to see. But after that we will move on to looking at what the chip does with these instructions after it decodes them. If you enjoyed the decoding homework, I think you’ll also enjoy simulating the behavior of the instructions, too!

1
Remember the x64 add instruction from the prologue of the course? This is essentially the same instruction! It was there right from the beginning, in the original 8086 and 8088.
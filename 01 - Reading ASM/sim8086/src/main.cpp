#include <array>
#include <iostream>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

enum class OperandKind {Register, Immediate, Memory};

// NOTE: Not used currently, but better than using strings in the future.
enum class MnemonicType
{
    mov,
    add,
    sub,
    cmp,
    unknown
};

struct Operand
{
    OperandKind kind = OperandKind::Register;
    uint8_t registerIndex = 0; // used when kind == Register
    uint16_t immediateValue = 0; // used when kind == Immediate
};

struct DecodeInstruction
{
    std::string mnemonic;
    std::string destination;
    std::string source;

    Operand destinationOperand;
    Operand sourceOperand;

    size_t size = 0;
    size_t offset = 0;

    bool hasJumpTarget = false;
    int jumpTarget = 0;
};

std::vector<uint8_t> ReadBinaryFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        throw std::runtime_error("Failed to open file: " + path);
    }

    file.seekg(0, std::ios::end);
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // std::vector's size constructor expects size_t (unsigned);
    // cast explicitly to avoid a signed-to-unsigned conversion warning.
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        throw std::runtime_error("Failed to read file: " + path);
    }

    return buffer;
}

void ensureBytesAvailable(const std::vector<uint8_t>& bytes, const size_t index, const size_t count, const char* context)
{
    // NOTE: Second check covers cases were index is valid,
    // but there are not enough remaining bytes to read count bytes.
    if (index > bytes.size() || count > bytes.size() - index)
    {
        throw std::runtime_error(std::string("Unexpected end of file while decoding ") + context);
    }
}

uint16_t readU16(const std::vector<uint8_t>& bytes, const size_t index, const char* context)
{
    ensureBytesAvailable(bytes, index, 2, context);

    const uint8_t lowByte = bytes[index];
    const uint8_t highByte = bytes[index + 1];

    return static_cast<uint16_t>(lowByte) | (static_cast<uint16_t>(highByte) << 8);
}

int8_t readI8(const std::vector<uint8_t>& bytes, const size_t index, const char* context)
{
    ensureBytesAvailable(bytes, index, 1, context);

    return static_cast<int8_t>(bytes[index]);
}

int16_t readI16(const std::vector<uint8_t>& bytes, const size_t index, const char* context)
{
    return static_cast<int16_t>(readU16(bytes, index, context));
}

std::string formatSigned8(const uint8_t value)
{
    return std::to_string(static_cast<int>(static_cast<int8_t>(value)));
}

std::string formatSigned16(const uint16_t value)
{
    return std::to_string(static_cast<int>(static_cast<int16_t>(value)));
}

std::string formatDisplacement(const int displacement)
{
    if (displacement < 0)
    {
        return " - " + std::to_string(-displacement);
    }

    return " + " + std::to_string(displacement);
}

const char* getRegisterName(const uint8_t regCode, const uint8_t w)
{
    static const char* reg8[8] =  {"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"};
    static const char* reg16[8] =  {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};

    if (regCode > 7)
    {
        throw std::runtime_error("Invalid register code");
    }

    return (w == 0) ? reg8[regCode] : reg16[regCode];
}

std::string getEffectiveAddressBase(const uint8_t rm)
{
    static const char* table[8] = {
        "bx + si",
        "bx + di",
        "bp + si",
        "bp + di",
        "si",
        "di",
        "bp",
        "bx"
    };

    if (rm > 7)
    {
        throw std::runtime_error("Invalid r/m code");
    }

    return table[rm];
}

std::string decodeRmOperand(
    const std::vector<uint8_t>& bytes,
    const size_t instructionIndex,
    const uint8_t mod,
    const uint8_t rm,
    const uint8_t w,
    const bool includeMemorySize,
    size_t& instructionSize)
{
    if (mod == 0b11)
    {
        return getRegisterName(rm, w);
    }

    std::string operand;
    // Special Case: direct 16 bit address
    if (mod == 0b00 && rm == 0b110)
    {
        const uint16_t address = readU16(bytes, instructionIndex + instructionSize, "direct address");
        instructionSize += 2;
        operand = "[" + std::to_string(address) + "]";
    }
    else if (mod == 0b00)
    {
        operand = "[" + getEffectiveAddressBase(rm) + "]";
    }
    else if (mod == 0b01)
    {
        const int8_t displacement = readI8(bytes, instructionIndex + instructionSize, "8-bit displacement");
        instructionSize += 1;
        operand = "[" + getEffectiveAddressBase(rm) + formatDisplacement(displacement) + "]";
    }
    else
    {
        const int16_t displacement = readI16(bytes, instructionIndex + instructionSize, "16-bit displacement");
        instructionSize += 2;
        operand = "[" + getEffectiveAddressBase(rm) + formatDisplacement(displacement) + "]";
    }

    if (includeMemorySize)
    {
        operand = std::string((w == 0) ? "byte " : "word ") + operand;
    }

    return operand;
}

bool isMovRegisterMemoryToFromRegister(const uint8_t byte)
{
    return (byte >> 2) == 0b100010;
}

DecodeInstruction decodeMovRegisterMemoryToFromRegister(const std::vector<uint8_t>& bytes, const size_t index)
{
    ensureBytesAvailable(bytes, index, 2, "mov register/memory to/from register");

    const uint8_t byte1 = bytes[index];
    const uint8_t byte2 = bytes[index + 1];

    if (!isMovRegisterMemoryToFromRegister(byte1))
    {
        throw std::runtime_error("Unsupported instruction (not a mov reg/mem-to/from-reg).");
    }

    const uint8_t d = (byte1 >> 1) & 0b1;
    const uint8_t w = byte1 & 0b1;

    const uint8_t mod = (byte2 >> 6) & 0b11;
    const uint8_t reg = (byte2 >> 3) & 0b111;
    const uint8_t rm = byte2 & 0b111;

    const std::string regOperand = getRegisterName(reg, w);
    // NOTE: instructionSize starts at 2 because byte1 & byte2 were already consumed.
    // decodeRmOperand may add displacement bytes.
    size_t instructionSize = 2;
    const std::string rmOperand = decodeRmOperand(bytes, index, mod, rm, w, false, instructionSize);

    DecodeInstruction result;
    result.mnemonic = "mov";
    result.size = instructionSize;

    // Reg field is unconditionally a register
    Operand regAsOperand;
    regAsOperand.kind = OperandKind::Register;
    regAsOperand.registerIndex = reg;

    // rm field is only a register when mod == 0b11; otherwise it's memory
    Operand rmAsOperand;
    if (mod == 0b11)
    {
        rmAsOperand.kind = OperandKind::Register;
        rmAsOperand.registerIndex = rm;
    }
    else
    {
        rmAsOperand.kind = OperandKind::Memory;
    }

    if (d == 1)
    {
        result.destination = regOperand;
        result.source = rmOperand;

        result.destinationOperand = regAsOperand;
        result.sourceOperand = rmAsOperand;
    }
    else
    {
        result.destination = rmOperand;
        result.source = regOperand;

        result.destinationOperand = rmAsOperand;
        result.sourceOperand = regAsOperand;
    }

    return result;
}

bool IsMovImmediateToRegister(const uint8_t byte1)
{
    return (byte1 >> 4) == 0b1011;
}

DecodeInstruction DecodeMovImmediateToRegister(const std::vector<uint8_t>& bytes, const size_t index)
{
    const uint8_t byte1 = bytes[index];
    if (!IsMovImmediateToRegister(byte1))
    {
        throw std::runtime_error("Unsupported instruction not a mov Immediate to Register instruction.");
    }

    const uint8_t w = (byte1 >> 3) & 0b1;
    const uint8_t reg = byte1 & 0b111;

    DecodeInstruction result;
    result.mnemonic = "mov";

    result.destinationOperand.kind = OperandKind::Register;
    result.destinationOperand.registerIndex = reg;
    result.destination = getRegisterName(reg, w);

    result.sourceOperand.kind = OperandKind::Immediate;

    if (w == 0)
    {
        ensureBytesAvailable(bytes, index + 1, 1, "8-bit immediate");
        const uint8_t immediate = bytes[index + 1];

        result.sourceOperand.immediateValue = immediate;
        result.source = formatSigned8(immediate);
        result.size = 2;
    }
    else
    {
        const uint16_t immediate = readU16(bytes, index + 1, "16-bit immediate");

        result.sourceOperand.immediateValue = immediate;
        result.source = formatSigned16(immediate);
        result.size = 3;
    }

    return result;
}

const char* getArithmeticMnemonic(const uint8_t operation)
{
    switch (operation)
    {
        case 0b000: return "add";
        case 0b101: return "sub";
        case 0b111: return "cmp";
        default: return nullptr;
    }
}

bool isArithmeticRegisterMemoryToFromRegister(const uint8_t byte1)
{
    const uint8_t operation = (byte1 >> 3) & 0b111;

    return ((byte1 & 0b11000100) == 0b00000000) && (getArithmeticMnemonic(operation) != nullptr);
}

DecodeInstruction decodeArithmeticRegisterMemoryToFromRegister(const std::vector<uint8_t>& bytes, const size_t index)
{
    ensureBytesAvailable(bytes, index, 2, "arithmetic register/memory to/from register");

    const uint8_t byte1 = bytes[index];
    const uint8_t byte2 = bytes[index + 1];

    const uint8_t operation = (byte1 >> 3) & 0b111;
    const char* mnemonic = getArithmeticMnemonic(operation);
    if (mnemonic == nullptr)
    {
        throw std::runtime_error("Unsupported arithmetic instruction.");
    }

    const uint8_t d = (byte1 >> 1) & 0b1;
    const uint8_t w = byte1 & 0b1;

    const uint8_t mod = (byte2 >> 6) & 0b11;
    const uint8_t reg = (byte2 >> 3) & 0b111;
    const uint8_t rm = byte2 & 0b111;

    const std::string regOperand = getRegisterName(reg, w);
    size_t instructionSize = 2;
    const std::string rmOperand = decodeRmOperand(bytes, index, mod, rm, w, false, instructionSize);

    DecodeInstruction result;
    result.mnemonic = mnemonic;
    result.size = instructionSize;

    // reg field is unconditionally a register
    Operand regAsOperand {};
    regAsOperand.kind = OperandKind::Register;
    regAsOperand.registerIndex = reg;

    // rm field is only a register when mod == 0b11; otherwise it's memory
    Operand rmAsOperand {};
    if (mod == 0b11)
    {
        rmAsOperand.kind = OperandKind::Register;
        rmAsOperand.registerIndex = rm;
    }
    else
    {
        rmAsOperand.kind = OperandKind::Memory;
    }

    if (d == 1)
    {
        result.destination = regOperand;
        result.source = rmOperand;

        result.destinationOperand = regAsOperand;
        result.sourceOperand = rmAsOperand;
    }
    else
    {
        result.destination = rmOperand;
        result.source = regOperand;

        result.destinationOperand = rmAsOperand;
        result.sourceOperand = regAsOperand;
    }

    return result;
}

bool isArithmeticImmediateToRegisterMemory(const uint8_t byte1)
{
    return (byte1 >> 2) == 0b100000;
}

DecodeInstruction decodeArithmeticImmediateToRegisterMemory(const std::vector<uint8_t>& bytes, const size_t index)
{
    ensureBytesAvailable(bytes, index, 2, "arithmetic immediate to register/memory");

    const uint8_t byte1 = bytes[index];
    const uint8_t byte2 = bytes[index + 1];

    const uint8_t s = (byte1 >> 1) & 0b1;
    const uint8_t w = byte1 & 0b1;

    const uint8_t mod = (byte2 >> 6) & 0b11;
    const uint8_t operation = (byte2 >> 3) & 0b111;
    const uint8_t rm = byte2 & 0b111;

    const char* mnemonic = getArithmeticMnemonic(operation);
    if (mnemonic == nullptr)
    {
        throw std::runtime_error("Unsupported arithmetic immediate instruction.");
    }

    size_t instructionSize = 2;
    const bool includeMemorySize = true;
    const std::string destination = decodeRmOperand(bytes, index, mod, rm, w, includeMemorySize, instructionSize);

    std::string source;
    if (s == 1 || w == 0)
    {
        ensureBytesAvailable(bytes, index + instructionSize, 1, "8-bit immediate");
        source = formatSigned8(bytes[index + instructionSize]);
        instructionSize += 1;
    }
    else
    {
        const uint16_t immediate = readU16(bytes, index + instructionSize, "16-bit immediate");
        source = formatSigned16(immediate);
        instructionSize += 2;
    }

    DecodeInstruction result;
    result.mnemonic = mnemonic;
    result.destination = destination;
    result.source = source;
    result.size = instructionSize;

    return result;
}

bool isArithmeticImmediateToAccumulator(const uint8_t byte1)
{
    const uint8_t operation = (byte1 >> 3) & 0b111;

    return ((byte1 & 0b11000110) == 0b00000100) && (getArithmeticMnemonic(operation) != nullptr);
}

DecodeInstruction decodeArithmeticImmediateToAccumulator(const std::vector<uint8_t>& bytes, const size_t index)
{
    const uint8_t byte1 = bytes[index];

    const uint8_t operation = (byte1 >> 3) & 0b111;
    const char* mnemonic = getArithmeticMnemonic(operation);
    if (mnemonic == nullptr)
    {
        throw std::runtime_error("Unsupported arithmetic accumulator instruction.");
    }

    const uint8_t w = byte1 & 0b1;

    DecodeInstruction result;
    result.mnemonic = mnemonic;
    result.destination = (w == 0) ? "al" : "ax";

    if (w == 0)
    {
        ensureBytesAvailable(bytes, index + 1, 1, "8-bit accumulator immediate");
        result.source = formatSigned8(bytes[index + 1]);
        result.size = 2;
    }
    else
    {
        const uint16_t immediate = readU16(bytes, index + 1, "16-bit accumulator immediate");
        result.source = formatSigned16(immediate);
        result.size = 3;
    }

    return result;
}

const char* getJumpMnemonic(const uint8_t byte1)
{
    static const char* conditionalJumps[16] = {
        "jo", "jno", "jb", "jnb",
        "je", "jnz", "jbe", "ja",
        "js", "jns", "jp", "jnp",
        "jl", "jnl", "jle", "jg"
    };

    if ((byte1 & 0b11110000) == 0b01110000)
    {
        return conditionalJumps[byte1 & 0b00001111];
    }

    switch (byte1)
    {
        case 0b11100000: return "loopnz";
        case 0b11100001: return "loopz";
        case 0b11100010: return "loop";
        case 0b11100011: return "jcxz";
        default: return nullptr;
    }
}

bool isJump(const uint8_t byte1)
{
    return getJumpMnemonic(byte1) != nullptr;
}

DecodeInstruction decodeJump(const std::vector<uint8_t>& bytes, const size_t index)
{
    ensureBytesAvailable(bytes, index, 2, "short jump");

    const uint8_t byte1 = bytes[index];
    const int8_t displacement = readI8(bytes, index + 1, "jump displacement");

    DecodeInstruction result;
    result.mnemonic = getJumpMnemonic(byte1);
    result.destination = std::to_string(static_cast<int>(displacement));
    result.size = 2;
    result.hasJumpTarget = true;
    result.jumpTarget = static_cast<int>(index) + static_cast<int>(result.size) + static_cast<int>(displacement);

    return result;
}

DecodeInstruction decodeInstruction(const std::vector<uint8_t>& bytes, const size_t index)
{
    const uint8_t byte1 = bytes[index];

    DecodeInstruction instruction;
    if (isMovRegisterMemoryToFromRegister(byte1))
    {
        instruction = decodeMovRegisterMemoryToFromRegister(bytes, index);
    }
    else if (IsMovImmediateToRegister(byte1))
    {
        instruction = DecodeMovImmediateToRegister(bytes, index);
    }
    else if (isArithmeticRegisterMemoryToFromRegister(byte1))
    {
        instruction = decodeArithmeticRegisterMemoryToFromRegister(bytes, index);
    }
    else if (isArithmeticImmediateToRegisterMemory(byte1))
    {
        instruction = decodeArithmeticImmediateToRegisterMemory(bytes, index);
    }
    else if (isArithmeticImmediateToAccumulator(byte1))
    {
        instruction = decodeArithmeticImmediateToAccumulator(bytes, index);
    }
    else if (isJump(byte1))
    {
        instruction = decodeJump(bytes, index);
    }
    else
    {
        throw std::runtime_error("Instruction is not supported.");
    }

    instruction.offset = index;

    return instruction;
}

std::vector<DecodeInstruction> DecodeInstructions(const std::vector<uint8_t>& bytes)
{
    std::vector<DecodeInstruction> instructions;

    for (size_t i = 0; i < bytes.size();)
    {
        DecodeInstruction instruction = decodeInstruction(bytes, i);
        i += instruction.size;
        instructions.push_back(instruction);
    }

    return instructions;
}

std::map<size_t, std::string> BuildJumpLabels(
    const std::vector<DecodeInstruction>& instructions,
    const size_t fileSize)
{
    std::set<size_t> instructionOffsets;
    for (const DecodeInstruction& instruction : instructions)
    {
        instructionOffsets.insert(instruction.offset);
    }

    std::set<size_t> targetOffsets;
    for (const DecodeInstruction& instruction : instructions)
    {
        if (!instruction.hasJumpTarget || instruction.jumpTarget < 0)
        {
            continue;
        }

        const auto targetOffset = static_cast<size_t>(instruction.jumpTarget);
        if (instructionOffsets.contains(targetOffset) || targetOffset == fileSize)
        {
            targetOffsets.insert(targetOffset);
        }
    }

    std::map<size_t, std::string> labels;
    int labelIndex = 0;
    for (const size_t targetOffset : targetOffsets)
    {
        labels[targetOffset] = "label" + std::to_string(labelIndex);
        ++labelIndex;
    }

    return labels;
}

void ApplyJumpLabels(
    std::vector<DecodeInstruction>& instructions,
    const std::map<size_t, std::string>& labels)
{
    for (DecodeInstruction& instruction : instructions)
    {
        if (!instruction.hasJumpTarget || instruction.jumpTarget < 0)
        {
            continue;
        }

        const auto label = labels.find(static_cast<size_t>(instruction.jumpTarget));
        if (label != labels.end())
        {
            instruction.destination = label->second;
        }
    }
}

void PrintInstruction(const DecodeInstruction& instruction)
{
    std::cout << instruction.mnemonic;

    if (!instruction.destination.empty())
    {
        std::cout << ' ' << instruction.destination;

        if (!instruction.source.empty())
        {
            std::cout << ", " << instruction.source;
        }
    }

    std::cout << '\n';
}

std::vector<DecodeInstruction> ReadAndDecode(const std::string& path)
{
    const std::vector<uint8_t> bytes = ReadBinaryFile(path);

    return DecodeInstructions(bytes);
}

size_t GetProgramFileSize(const std::vector<DecodeInstruction>& instructions)
{
    if (instructions.empty()) return 0;

    const DecodeInstruction& last = instructions.back();

    return last.offset + last.size;
}

void DecodeFile(const std::string& path)
{
    std::vector<DecodeInstruction> instructions = ReadAndDecode(path);
    const size_t fileSize = GetProgramFileSize(instructions);

    const std::map<size_t, std::string> labels = BuildJumpLabels(instructions, fileSize);
    ApplyJumpLabels(instructions, labels);

    // NOTE: NASM comment
    std::cout << "; " << path << " disassembly\n";
    std::cout << "bits 16\n\n";

    for (const DecodeInstruction& instruction : instructions)
    {
        if (const auto label = labels.find(instruction.offset); label != labels.end())
        {
            std::cout << '\n' << label->second << ":\n";
        }

        PrintInstruction(instruction);
    }

    if (const auto endLabel = labels.find(fileSize); endLabel != labels.end())
    {
        std::cout << '\n' << endLabel->second << ":\n";
    }
}

uint16_t ReadOperandValue(const std::array<uint16_t, 8>& registers, const Operand& operand)
{
    switch (operand.kind)
    {
        case OperandKind::Register: return registers[operand.registerIndex];
        case OperandKind::Immediate: return operand.immediateValue;
        case OperandKind::Memory: throw std::runtime_error("Not yet implemented");
        default: throw std::runtime_error("Couldn't find specified Operand Kind.");
    }
}

void ExecuteInstruction(std::array<uint16_t, 8>& registers, const DecodeInstruction& instruction, bool& zeroFlag, bool& signFlag)
{
    if (instruction.destinationOperand.kind != OperandKind::Register) throw std::runtime_error("Destination Operand must be a Register");

    if (instruction.mnemonic == "mov")
    {
        const uint16_t source = ReadOperandValue(registers, instruction.sourceOperand);
        registers[instruction.destinationOperand.registerIndex] = source;
    }
    else if (instruction.mnemonic == "add")
    {
        throw std::runtime_error("add not supported yet.");
    }
    else if (instruction.mnemonic == "sub")
    {
        const uint16_t destinationValue = ReadOperandValue(registers, instruction.destinationOperand);
        const uint16_t sourceValue = ReadOperandValue(registers, instruction.sourceOperand);

        const uint16_t result = destinationValue - sourceValue;
        zeroFlag = result == 0;
        signFlag = (result >> 15) & 0b1;

        registers[instruction.destinationOperand.registerIndex] = result;
    }
    else if (instruction.mnemonic == "cmp")
    {
        throw std::runtime_error("cmp not supported yet.");
    }
    else
    {
        throw std::runtime_error("Unsupported mnemonic: " + instruction.mnemonic);
    }
}

void PrintInstruction(const DecodeInstruction& instruction, const bool newline = true)
{
    std::cout << instruction.mnemonic;

    if (!instruction.destination.empty())
    {
        std::cout << ' ' << instruction.destination;

        if (!instruction.source.empty())
        {
            std::cout << ", " << instruction.source;
        }
    }

    if (newline)
    {
        std::cout << '\n';
    }
}

std::string FormatFlags(const bool zeroFlag, const bool signFlag)
{
    std::string flags;
    if (zeroFlag) flags += 'Z';
    if (signFlag) flags += 'S';

    return flags;
}

void SimulateFile(const std::string& path)
{
    const std::vector<DecodeInstruction> instructions = ReadAndDecode(path);
    std::array<uint16_t, 8> registers {};

    bool zeroFlag {};
    bool signFlag {};

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

// argc = argument count, argv = argument vector - C Style string array.
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2)
        {
            DecodeFile(argv[1]);
        }
        else if (argc == 3 && std::string(argv[1]) == "-exec")
        {
            SimulateFile(argv[2]);
        }
        // No binary file name specified, just: .\cmake-build-debug\sim8086.exe
        else
        {
            std::cerr << "Usage: sim8086 [-exec] <binary-file>\n";
            return 1;
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << '\n';

        return 1;
    }
}

#include <iostream>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct DecodeInstruction
{
    std::string mnemonic;
    std::string destination;
    std::string source;
    size_t size;
};


std::vector<uint8_t> readBinaryFile(const std::string& path)
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


bool isMovRegisterMemoryToFromRegister(const uint8_t byte1)
{
    return (byte1 >> 2) == 0b100010;
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
        throw::std::runtime_error("Invalid r/m code");
    }

    return table[rm];
}

DecodeInstruction decodeMovRegisterMemoryToFromRegister(const std::vector<uint8_t>& bytes, const size_t index)
{
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
    std::string rmOperand;
    size_t instructionSize = 2;

    if (mod == 0b11)
    {
        rmOperand = getRegisterName(rm, w);
    }
    else if (mod == 0b01)
    {
        instructionSize = 3;
        const uint8_t displacement = bytes[index + 2];

        rmOperand = "[" + getEffectiveAddressBase(rm) + " + " + std::to_string(displacement) + "]";
    }
    else if (mod == 0b10)
    {
        instructionSize = 4;
        const uint8_t lowByte = bytes[index + 2];
        const uint8_t highByte = bytes[index + 3];
        const uint16_t displacement = static_cast<uint16_t>(lowByte) | (static_cast<uint16_t>(highByte) << 8);

        rmOperand = "[" + getEffectiveAddressBase(rm) + " + " + std::to_string(displacement) + "]";
    }
    else
    {
        rmOperand = "[" + getEffectiveAddressBase(rm) + "]";
    }

    DecodeInstruction result;
    result.mnemonic = "mov";
    result.size = instructionSize;
    if (d == 1)
    {
        result.destination = regOperand;
        result.source = rmOperand;
    }
    else
    {
        result.destination = rmOperand;
        result.source = regOperand;
    }

    return result;
}

bool isMovImmediateToRegister(const uint8_t byte1)
{
    return (byte1 >> 4) == 0b1011;
}

DecodeInstruction decodeMovImmediateToRegister(const std::vector<uint8_t>& bytes, const size_t index)
{
    const uint8_t byte1 = bytes[index];
    if (!isMovImmediateToRegister(byte1))
    {
        throw std::runtime_error("Unsupported instruction not a mov Immediate to Register instruction.");
    }

    const uint8_t w = (byte1 >> 3) & 0b1;
    const uint8_t reg = byte1 & 0b111;

    DecodeInstruction result;
    result.mnemonic = "mov";
    result.destination = getRegisterName(reg, w);

    if (w == 0)
    {
        if (index + 1 >= bytes.size())
        {
            throw std::runtime_error("unexpected end of file while decoding 8-bit immediate");
        }

        const uint8_t immediate = bytes[index + 1];

        result.source = std::to_string(immediate);
        result.size = 2;
    }
    else
    {
        if (index + 2 >= bytes.size())
        {
            throw std::runtime_error("unexpected end of file while decoding 16-bit immediate");
        }

        const uint8_t lowByte = bytes[index + 1];
        const uint8_t highByte = bytes[index + 2];

        const uint16_t immediate = static_cast<uint16_t>(lowByte) | (static_cast<uint16_t>(highByte) << 8);

        result.source = std::to_string(immediate);
        result.size = 3;
    }

    return result;
}

void decodeFile(const std::string& path)
{
    const std::vector<uint8_t> bytes = readBinaryFile(path);

    // NOTE: NASM comment
    std::cout << "; " << path << " disassembly\n";
    std::cout << "bits 16\n\n";



    for (size_t i = 0; i < bytes.size();)
    {
        // NOTE: For debugging only:
        std::cerr << "i = " << i
              << ", byte = " << static_cast<int>(bytes[i])
              << '\n';

        DecodeInstruction instruction;
        if (isMovRegisterMemoryToFromRegister(bytes[i]))
        {
            if (i + 1 >= bytes.size())
            {
                throw std::runtime_error("Unexpected end of file while decoding instruction.");
            }

            std::cerr << "  decoder: mov reg/mem to/from reg\n";

            instruction = decodeMovRegisterMemoryToFromRegister(bytes, i);
        }
        else if (isMovImmediateToRegister(bytes[i]))
        {
            std::cerr << "  decoder: mov immediate to register\n";
            instruction = decodeMovImmediateToRegister(bytes, i);
        }
        else
        {
            throw std::runtime_error("Instruction is not supported.");
        }


        std::cout << instruction.mnemonic << ' '
                  << instruction.destination << ", "
                  << instruction.source << '\n';

        i += instruction.size;
    }
}

// argc = argument count, argv = argument vector - C Style string array.
int main(int argc, char** argv)
{
    try
    {
        // No binary file name specified, just: .\cmake-build-debug\sim8086.exe
        if (argc < 2)
        {
            std::cerr << "Usage: sim8086 <binary-file>\n";

            return 1;
        }

        // given: .\cmake-build-debug\sim8086.exe .\data\listing_0037 in the terminal:
        // argv[0] = sim8086.exe
        // argv[1] = .\data\listing_0037
        decodeFile(argv[1]);

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << '\n';

        return 1;
    }
}
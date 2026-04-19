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

DecodeInstruction decodeMovRegisterToRegister(const uint8_t byte1, const uint8_t byte2)
{
    if (!isMovRegisterMemoryToFromRegister(byte1))
    {
        throw std::runtime_error("Unsupported instruction (not a mov reg/mem-to/from-reg).");
    }

    const uint8_t d = (byte1 >> 1) & 0b1;
    const uint8_t w = byte1 & 0b1;

    const uint8_t mod = (byte2 >> 6) & 0b11;
    const uint8_t reg = (byte2 >> 3) & 0b111;
    const uint8_t rm = byte2 & 0b111;

    if (mod!= 0b11)
    {
        throw std::runtime_error("Unsupported mod (only register-to-register supported).");
    }

    const std::string regOperand = getRegisterName(reg, w);
    const std::string rmOperand = getRegisterName(rm, w);

    DecodeInstruction result;
    result.mnemonic = "mov";
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

void decodeFile(const std::string& path)
{
    const std::vector<uint8_t> bytes = readBinaryFile(path);

    // NOTE: NASM comment
    std::cout << "; " << path << " disassembly\n";
    std::cout << "bits 16\n\n";

    for (size_t i = 0; i < bytes.size(); )
    {
        if (i + 1 >= bytes.size())
        {
            throw std::runtime_error("Unexpected end of file while decoding instruction.");
        }

        DecodeInstruction instruction = decodeMovRegisterToRegister(bytes[i], bytes[i + 1]);

        std::cout << instruction.mnemonic << ' '
                  << instruction.destination << ", "
                  << instruction.source << '\n';

        i += 2;
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
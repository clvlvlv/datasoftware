#ifndef DATASOFTWARE_COMPRESSOR_H
#define DATASOFTWARE_COMPRESSOR_H

#include <vector>
#include <cstdint>
#include <string>

namespace datasoftware {

enum class CompressAlgo : uint8_t {
    RLE     = 0,
    LZ77    = 1,
    Huffman = 2,
};

class Compressor {
public:
    static std::vector<char> compress(const std::vector<char>& input,
                                      CompressAlgo algo);
    static std::vector<char> decompress(const std::vector<char>& input,
                                        CompressAlgo algo);
    static void compressFile(const std::string& inputPath,
                             const std::string& outputPath,
                             CompressAlgo algo);
    static void decompressFile(const std::string& inputPath,
                               const std::string& outputPath,
                               CompressAlgo algo);
    static const char* extension(CompressAlgo algo);
    static CompressAlgo detectAlgo(const std::vector<char>& input);
    static const char* name(CompressAlgo algo);

private:
    // ---- RLE ----
    static std::vector<char> compressRLE(const std::vector<char>& input);
    static std::vector<char> decompressRLE(const std::vector<char>& input);

    // ---- LZ77 ----
    static constexpr size_t LZ_WINDOW = 4096;
    static constexpr size_t LZ_LOOKAHEAD = 64;
    static std::vector<char> compressLZ77(const std::vector<char>& input);
    static std::vector<char> decompressLZ77(const std::vector<char>& input);

    // ---- Huffman ----
    static std::vector<char> compressHuffman(const std::vector<char>& input);
    static std::vector<char> decompressHuffman(const std::vector<char>& input);

public:
    struct HuffNode {
        int      symbol = -1;
        uint64_t freq   = 0;
        HuffNode* left  = nullptr;
        HuffNode* right = nullptr;
        ~HuffNode() { delete left; delete right; }
    };

private:
    struct HuffCode {
        uint32_t bits = 0;
        int      len  = 0;
    };

    static HuffNode* buildHuffTree(const uint64_t freq[256]);
    static void buildHuffCodes(HuffNode* node, uint32_t bits, int len,
                               HuffCode codes[256]);
};

} // namespace datasoftware

#endif // DATASOFTWARE_COMPRESSOR_H
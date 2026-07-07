#include "datasoftware/Compressor.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <queue>
#include <vector>

namespace datasoftware {

// ====== Public API ======

std::vector<char> Compressor::compress(const std::vector<char>& input,
                                        CompressAlgo algo) {
    switch (algo) {
    case CompressAlgo::RLE:     return compressRLE(input);
    case CompressAlgo::LZ77:    return compressLZ77(input);
    case CompressAlgo::Huffman: return compressHuffman(input);
    default: throw std::runtime_error("Unknown compression algorithm");
    }
}

std::vector<char> Compressor::decompress(const std::vector<char>& input,
                                          CompressAlgo algo) {
    // Auto-detect by magic
    if (input.size() >= 4) {
        if (input[0]=='D' && input[1]=='S' && input[2]=='R' && input[3]=='L')
            return decompressRLE(input);
        if (input[0]=='D' && input[1]=='S' && input[2]=='L' && input[3]=='Z')
            return decompressLZ77(input);
        if (input[0]=='D' && input[1]=='S' && input[2]=='H' && input[3]=='F')
            return decompressHuffman(input);
    }
    switch (algo) {
    case CompressAlgo::RLE:     return decompressRLE(input);
    case CompressAlgo::LZ77:    return decompressLZ77(input);
    case CompressAlgo::Huffman: return decompressHuffman(input);
    default: throw std::runtime_error("Unknown compression algorithm");
    }
}

void Compressor::compressFile(const std::string& inputPath,
                               const std::string& outputPath,
                               CompressAlgo algo) {
    std::ifstream in(inputPath, std::ios::binary | std::ios::ate);
    if (!in.is_open())
        throw std::runtime_error("Cannot open input file: " + inputPath);
    auto size = in.tellg();
    in.seekg(0);
    std::vector<char> buf(static_cast<size_t>(size));
    if (size > 0) in.read(buf.data(), size);
    in.close();

    auto compressed = compress(buf, algo);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("Cannot create output file: " + outputPath);
    out.write(compressed.data(), compressed.size());
    out.close();
}

void Compressor::decompressFile(const std::string& inputPath,
                                 const std::string& outputPath,
                                 CompressAlgo algo) {
    std::ifstream in(inputPath, std::ios::binary | std::ios::ate);
    if (!in.is_open())
        throw std::runtime_error("Cannot open input file: " + inputPath);
    auto size = in.tellg();
    in.seekg(0);
    std::vector<char> buf(static_cast<size_t>(size));
    if (size > 0) in.read(buf.data(), size);
    in.close();

    auto decompressed = decompress(buf, algo);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("Cannot create output file: " + outputPath);
    out.write(decompressed.data(), decompressed.size());
    out.close();
}

const char* Compressor::extension(CompressAlgo algo) {
    switch (algo) {
    case CompressAlgo::RLE:     return ".rlz";
    case CompressAlgo::LZ77:    return ".lz77";
    case CompressAlgo::Huffman: return ".huf";
    default: return ".comp";
    }
}

CompressAlgo Compressor::detectAlgo(const std::vector<char>& input) {
    if (input.size() >= 4) {
        if (input[0]=='D' && input[1]=='S' && input[2]=='R' && input[3]=='L')
            return CompressAlgo::RLE;
        if (input[0]=='D' && input[1]=='S' && input[2]=='L' && input[3]=='Z')
            return CompressAlgo::LZ77;
        if (input[0]=='D' && input[1]=='S' && input[2]=='H' && input[3]=='F')
            return CompressAlgo::Huffman;
    }
    throw std::runtime_error("Cannot detect compression algorithm from header");
}

const char* Compressor::name(CompressAlgo algo) {
    switch (algo) {
    case CompressAlgo::RLE:     return "RLE (Run-Length Encoding)";
    case CompressAlgo::LZ77:    return "LZ77 (Sliding Window)";
    case CompressAlgo::Huffman: return "Huffman Coding";
    default: return "Unknown";
    }
}

// ====== RLE Implementation ======
// File format: "DSRL" magic (4B) + version (uint32_t) + body
// Body: chunks of:
//   [flags: uint8_t] bit0: 0=literal-run, 1=repeat-run
//   [count: uint16_t] (1-65535, 0 means 65536)
//   If literal-run: [count bytes of data]
//   If repeat-run:  [1 byte value]

std::vector<char> Compressor::compressRLE(const std::vector<char>& input) {
    std::vector<char> out;
    // Header
    out.push_back('D'); out.push_back('S');
    out.push_back('R'); out.push_back('L');
    uint32_t ver = 1;
    auto* vp = reinterpret_cast<const char*>(&ver);
    out.insert(out.end(), vp, vp + 4);

    size_t i = 0;
    while (i < input.size()) {
        // Count consecutive identical bytes
        size_t runStart = i;
        uint8_t val = static_cast<uint8_t>(input[i]);
        while (i < input.size() && static_cast<uint8_t>(input[i]) == val && (i - runStart) < 65536) {
            ++i;
        }
        size_t runLen = i - runStart;

        if (runLen >= 3) {
            // Repeat-run: store as run
            uint16_t count = static_cast<uint16_t>(std::min(runLen, size_t(65536)));
            uint8_t flags = 1; // repeat-run
            out.push_back(static_cast<char>(flags));
            auto* cp = reinterpret_cast<const char*>(&count);
            out.insert(out.end(), cp, cp + 2);
            out.push_back(static_cast<char>(val));
        } else {
            // Literal-run: store raw bytes
            uint16_t count = static_cast<uint16_t>(runLen);
            uint8_t flags = 0; // literal-run
            out.push_back(static_cast<char>(flags));
            auto* cp = reinterpret_cast<const char*>(&count);
            out.insert(out.end(), cp, cp + 2);
            for (size_t j = runStart; j < i; ++j) {
                out.push_back(input[j]);
            }
        }
    }

    return out;
}

std::vector<char> Compressor::decompressRLE(const std::vector<char>& input) {
    if (input.size() < 8) throw std::runtime_error("Invalid RLE data (too short)");
    if (input[0]!='D'||input[1]!='S'||input[2]!='R'||input[3]!='L')
        throw std::runtime_error("Invalid RLE magic");

    std::vector<char> out;
    size_t pos = 8; // skip header (4 magic + 4 version)

    while (pos < input.size()) {
        if (pos + 3 > input.size()) throw std::runtime_error("Invalid RLE chunk header");
        uint8_t flags = static_cast<uint8_t>(input[pos++]);
        uint16_t count;
        std::memcpy(&count, &input[pos], 2);
        pos += 2;

        bool isRepeat = (flags & 1) != 0;
        if (isRepeat) {
            if (pos + 1 > input.size()) throw std::runtime_error("Invalid RLE run chunk");
            char val = input[pos++];
            out.insert(out.end(), count, val);
        } else {
            if (pos + count > input.size()) throw std::runtime_error("Invalid RLE literal chunk");
            out.insert(out.end(), input.begin() + pos, input.begin() + pos + count);
            pos += count;
        }
    }

    return out;
}

// ====== LZ77 Implementation ======
// File format: "DSLZ" magic (4B) + version (uint32_t) + body
// Body: sequence of tokens:
//   [flags: uint8_t] bit0: 0=literal, 1=(offset,length) pair
//   If literal: [1 byte]
//   If pair:    [offset: uint16_t] [length: uint16_t]

std::vector<char> Compressor::compressLZ77(const std::vector<char>& input) {
    std::vector<char> out;
    // Header
    out.push_back('D'); out.push_back('S');
    out.push_back('L'); out.push_back('Z');
    uint32_t ver = 1;
    auto* vp = reinterpret_cast<const char*>(&ver);
    out.insert(out.end(), vp, vp + 4);

    size_t i = 0;
    size_t inSize = input.size();

    while (i < inSize) {
        // Search in sliding window for the longest match
        size_t windowStart = (i > LZ_WINDOW) ? i - LZ_WINDOW : 0;
        size_t matchLen = 0;
        size_t matchOffset = 0;
        size_t maxLookahead = std::min(LZ_LOOKAHEAD, inSize - i);

        for (size_t w = windowStart; w < i; ++w) {
            size_t len = 0;
            while (len < maxLookahead && (w + len) < i && input[w + len] == input[i + len]) {
                ++len;
            }
            if (len > matchLen) {
                matchLen = len;
                matchOffset = i - w;
            }
        }

        if (matchLen >= 3 && matchOffset <= 0xFFFF) {
            // Emit (offset, length) pair
            uint8_t flags = 1;
            out.push_back(static_cast<char>(flags));
            uint16_t off = static_cast<uint16_t>(matchOffset);
            uint16_t len = static_cast<uint16_t>(std::min(matchLen, size_t(0xFFFF)));
            auto* op = reinterpret_cast<const char*>(&off);
            out.insert(out.end(), op, op + 2);
            auto* lp = reinterpret_cast<const char*>(&len);
            out.insert(out.end(), lp, lp + 2);
            i += len;
        } else {
            // Emit literal byte
            uint8_t flags = 0;
            out.push_back(static_cast<char>(flags));
            out.push_back(input[i]);
            ++i;
        }
    }

    return out;
}

std::vector<char> Compressor::decompressLZ77(const std::vector<char>& input) {
    if (input.size() < 8) throw std::runtime_error("Invalid LZ77 data (too short)");
    if (input[0]!='D'||input[1]!='S'||input[2]!='L'||input[3]!='Z')
        throw std::runtime_error("Invalid LZ77 magic");

    std::vector<char> out;
    size_t pos = 8;

    while (pos < input.size()) {
        if (pos + 1 > input.size()) throw std::runtime_error("Invalid LZ77 token");
        uint8_t flags = static_cast<uint8_t>(input[pos++]);

        if ((flags & 1) == 0) {
            // Literal
            out.push_back(input[pos++]);
        } else {
            // (offset, length) pair
            if (pos + 4 > input.size()) throw std::runtime_error("Invalid LZ77 pair");
            uint16_t offset, length;
            std::memcpy(&offset, &input[pos], 2);
            std::memcpy(&length, &input[pos + 2], 2);
            pos += 4;

            if (offset == 0) throw std::runtime_error("Invalid LZ77 offset (0)");
            size_t start = out.size() - offset;
            for (uint16_t j = 0; j < length; ++j) {
                out.push_back(out[start + j]);
            }
        }
    }

    return out;
}

// ====== Huffman Implementation ======
// Format: "DSHF" magic (4B) + version (uint32_t) + origSize (uint64_t)
//   + validBits (uint32_t, total valid bits in encoded stream)
//   + codeLen[256] (256 bytes, code length per byte, 0 = unused)
//   + encoded bits packed into bytes, MSB first

struct FreqNode {
    uint64_t freq;
    int      symbol; // -1 for internal
    Compressor::HuffNode* node;

    bool operator>(const FreqNode& o) const { return freq > o.freq; }
};

Compressor::HuffNode* Compressor::buildHuffTree(const uint64_t freq[256]) {
    std::priority_queue<FreqNode, std::vector<FreqNode>, std::greater<FreqNode>> pq;

    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            auto* n = new HuffNode;
            n->symbol = i;
            n->freq = freq[i];
            pq.push({freq[i], i, n});
        }
    }

    // Single symbol special case
    if (pq.size() == 1) {
        auto* n = new HuffNode;
        n->freq = pq.top().freq;
        n->left = pq.top().node;
        pq.pop();
        return n;
    }

    while (pq.size() > 1) {
        auto a = pq.top(); pq.pop();
        auto b = pq.top(); pq.pop();

        auto* parent = new HuffNode;
        parent->freq = a.freq + b.freq;
        parent->left = a.node;
        parent->right = b.node;
        pq.push({parent->freq, -1, parent});
    }

    return pq.empty() ? nullptr : pq.top().node;
}

void Compressor::buildHuffCodes(HuffNode* node, uint32_t bits, int len,
                                 HuffCode codes[256]) {
    if (!node) return;

    if (node->symbol >= 0) {
        codes[node->symbol].bits = bits;
        codes[node->symbol].len  = len;
        return;
    }

    buildHuffCodes(node->left,  (bits << 1) | 0, len + 1, codes);
    buildHuffCodes(node->right, (bits << 1) | 1, len + 1, codes);
}

std::vector<char> Compressor::compressHuffman(const std::vector<char>& input) {
    // 1. Count frequencies
    uint64_t freq[256] = {0};
    for (auto c : input)
        freq[static_cast<uint8_t>(c)]++;

    // 2. Build tree and extract code LENGTHS
    auto* root = buildHuffTree(freq);
    HuffCode treeCodes[256] = {{0, 0}};
    if (root) buildHuffCodes(root, 0, 0, treeCodes);

    // 3. Rebuild canonical codes from lengths (same as decoder uses)
    int maxLen = 0;
    for (int i = 0; i < 256; ++i)
        if (treeCodes[i].len > maxLen) maxLen = treeCodes[i].len;

    HuffCode canonical[256] = {{0, 0}};
    if (maxLen > 0) {
        int blCount[256] = {0};
        for (int i = 0; i < 256; ++i)
            if (treeCodes[i].len > 0) blCount[treeCodes[i].len]++;

        uint32_t code = 0;
        uint32_t nextCode[256];
        for (int l = 1; l <= maxLen; ++l) {
            code = (code + blCount[l - 1]) << 1;
            nextCode[l] = code;
        }

        for (int i = 0; i < 256; ++i) {
            if (treeCodes[i].len > 0) {
                canonical[i].bits = nextCode[treeCodes[i].len]++;
                canonical[i].len  = treeCodes[i].len;
            }
        }
    }

    // 3. Pre-allocate header space: magic(4)+version(4)+origSize(8)+validBits(4)+codeLen(256)=276
    std::vector<char> out(276, 0);
    out[0]='D'; out[1]='S'; out[2]='H'; out[3]='F';
    uint32_t ver = 1;
    std::memcpy(&out[4], &ver, 4);
    uint64_t origSize = input.size();
    std::memcpy(&out[8], &origSize, 8);
    // validBits at offset 16 and codeLen at offset 20 filled after encoding

    // 4. Encode bitstream — byte-level MSB-first packing (no endian issues)
    std::vector<char> bitBytes;
    uint8_t curByte = 0;
    int bitsInCur = 0;

    for (auto c : input) {
        uint8_t sym = static_cast<uint8_t>(c);
        if (canonical[sym].len == 0) continue;

        uint32_t code = canonical[sym].bits;
        int codeLen = canonical[sym].len;

        for (int b = codeLen - 1; b >= 0; --b) {
            curByte = (curByte << 1) | ((code >> b) & 1);
            bitsInCur++;
            if (bitsInCur == 8) {
                bitBytes.push_back(static_cast<char>(curByte));
                curByte = 0;
                bitsInCur = 0;
            }
        }
    }

    // Flush remaining bits
    if (bitsInCur > 0) {
        curByte <<= (8 - bitsInCur);
        bitBytes.push_back(static_cast<char>(curByte));
    }

    // Append encoded bytes to output
    out.insert(out.end(), bitBytes.begin(), bitBytes.end());

    // 5. Fill in header fields
    uint64_t totalValidBits = 0;
    for (auto c : input) {
        uint8_t sym = static_cast<uint8_t>(c);
        if (canonical[sym].len > 0) totalValidBits += canonical[sym].len;
    }
    uint32_t validBits32 = static_cast<uint32_t>(totalValidBits);
    std::memcpy(&out[16], &validBits32, 4);
    for (int i = 0; i < 256; ++i)
        out[20 + i] = static_cast<char>(canonical[i].len);

    delete root;
    return out;
}

std::vector<char> Compressor::decompressHuffman(const std::vector<char>& input) {
    if (input.size() < 20) throw std::runtime_error("Invalid Huffman data (too short)");
    if (input[0]!='D'||input[1]!='S'||input[2]!='H'||input[3]!='F')
        throw std::runtime_error("Invalid Huffman magic");

    // Header: magic(4) + version(4) + origSize(8) + validBits(4) + codeLen(256)
    uint64_t origSize;
    std::memcpy(&origSize, &input[8], 8);

    uint32_t validBits;
    std::memcpy(&validBits, &input[16], 4);

    // Read code lengths (256 bytes at offset 20)
    int codeLen[256];
    for (int i = 0; i < 256; ++i)
        codeLen[i] = static_cast<uint8_t>(input[20 + i]);

    // 3. Rebuild canonical codes from lengths
    // Count codes per length
    int maxLen = 0;
    for (int i = 0; i < 256; ++i)
        if (codeLen[i] > maxLen) maxLen = codeLen[i];

    // Build canonical Huffman codes from lengths
    uint32_t codeVals[256] = {0};
    if (maxLen > 0) {
        // Step 1: count number of codes of each length
        int blCount[256] = {0}; // max length max 255
        for (int i = 0; i < 256; ++i)
            if (codeLen[i] > 0) blCount[codeLen[i]]++;

        // Step 2: assign base codes
        uint32_t code = 0;
        uint32_t nextCode[256];
        for (int len = 1; len <= maxLen; ++len) {
            code = (code + blCount[len - 1]) << 1;
            nextCode[len] = code;
        }

        // Step 3: assign codes to symbols
        for (int i = 0; i < 256; ++i) {
            if (codeLen[i] > 0) {
                codeVals[i] = nextCode[codeLen[i]]++;
            }
        }
    }

    // 4. Build tree from codes for decoding
    auto* root = new HuffNode;
    bool hasSymbols = false;
    for (int i = 0; i < 256; ++i) {
        if (codeLen[i] == 0) continue;
        hasSymbols = true;

        // Insert code into tree
        HuffNode* cur = root;
        uint32_t cv = codeVals[i];
        int len = codeLen[i];

        for (int b = len - 1; b >= 0; --b) {
            bool bit = (cv >> b) & 1;
            if (bit == 0) {
                if (!cur->left) cur->left = new HuffNode;
                cur = cur->left;
            } else {
                if (!cur->right) cur->right = new HuffNode;
                cur = cur->right;
            }
        }
        cur->symbol = i;
    }

    // Special case: single symbol
    if (!hasSymbols) {
        delete root;
        return std::vector<char>(origSize, 0);
    }

    // 5. Decode bits — byte-level MSB-first reading
    std::vector<char> out;
    out.reserve(static_cast<size_t>(origSize));

    size_t bytePos = 20 + 256; // start of encoded data
    uint64_t bitsRead = 0;
    int bitOff = 0; // bit offset within current byte (0 = MSB, 7 = LSB)

    // Single-symbol case
    bool singleSym = (!root->left && !root->right && root->symbol >= 0);
    if (singleSym) {
        while (out.size() < origSize)
            out.push_back(static_cast<char>(root->symbol));
        delete root;
        return out;
    }

    while (out.size() < origSize && bitsRead < validBits) {
        // Walk tree from root
        HuffNode* cur = root;
        while (cur && cur->symbol < 0) {
            if (bytePos >= input.size() || bitsRead >= validBits) break;
            uint8_t byte = static_cast<uint8_t>(input[bytePos]);
            int bit = (byte >> (7 - bitOff)) & 1;
            bitOff++;
            bitsRead++;
            if (bitOff == 8) { bitOff = 0; bytePos++; }

            cur = (bit == 0) ? cur->left : cur->right;

            if (cur && cur->symbol >= 0) {
                out.push_back(static_cast<char>(cur->symbol));
                if (out.size() >= origSize) break;
            }
        }
    }

    delete root;
    if (out.size() != origSize)
        throw std::runtime_error("Huffman decompression size mismatch");
    return out;
}

} // namespace datasoftware
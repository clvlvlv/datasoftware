#include "datasoftware/Crypto.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")



namespace datasoftware {

// ---- helpers ----

static NTSTATUS bcryptHash(BCRYPT_ALG_HANDLE hAlgo,
                           PUCHAR secret, ULONG secretLen,
                           PUCHAR input, ULONG inputLen,
                           PUCHAR output, ULONG outputLen) {
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS status = BCryptCreateHash(hAlgo, &hHash, nullptr, 0,
                                       secret, secretLen, 0);
    if (status < 0) return status;
    status = BCryptHashData(hHash, input, inputLen, 0);
    if (status < 0) { BCryptDestroyHash(hHash); return status; }
    status = BCryptFinishHash(hHash, output, outputLen, 0);
    BCryptDestroyHash(hHash);
    return status;
}

static void checkBCrypt(NTSTATUS status, const char* msg) {
    if (status < 0) {
        throw std::runtime_error(std::string("Crypto error: ") + msg +
                                 " (0x" + std::to_string(status) + ")");
    }
}

static std::vector<char> deriveKey(const std::string& password,
                                    const uint8_t salt[16],
                                    const char* context) {
    // Key = SHA-256(context + password + salt)
    BCRYPT_ALG_HANDLE hAlgo = nullptr;
    checkBCrypt(BCryptOpenAlgorithmProvider(&hAlgo, BCRYPT_SHA256_ALGORITHM,
                                            nullptr, 0),
                "Open SHA-256");

    // Build input: context + password + salt
    std::string ctx(context ? context : "");
    size_t inLen = ctx.size() + password.size() + 16;
    std::vector<uint8_t> input(inLen);
    size_t pos = 0;
    std::memcpy(&input[pos], ctx.data(), ctx.size()); pos += ctx.size();
    std::memcpy(&input[pos], password.data(), password.size()); pos += password.size();
    std::memcpy(&input[pos], salt, 16);

    uint8_t hash[32];
    ULONG resultLen = 0;
    checkBCrypt(bcryptHash(hAlgo, nullptr, 0,
                           input.data(), static_cast<ULONG>(inLen),
                           hash, sizeof(hash)),
                "Hash");
    BCryptCloseAlgorithmProvider(hAlgo, 0);

    std::vector<char> key(32);
    std::memcpy(key.data(), hash, 32);
    return key;
}

static std::vector<char> hmacSha256(const std::vector<char>& key,
                                     const std::vector<char>& data) {
    BCRYPT_ALG_HANDLE hAlgo = nullptr;
    checkBCrypt(BCryptOpenAlgorithmProvider(&hAlgo, BCRYPT_SHA256_ALGORITHM,
                                            nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG),
                "Open HMAC-SHA256");

    uint8_t hash[32];
    checkBCrypt(bcryptHash(hAlgo,
                reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
                static_cast<ULONG>(key.size()),
                reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                static_cast<ULONG>(data.size()),
                hash, sizeof(hash)),
                "HMAC");

    BCryptCloseAlgorithmProvider(hAlgo, 0);

    std::vector<char> mac(32);
    std::memcpy(mac.data(), hash, 32);
    return mac;
}

// ---- Public API ----

bool Crypto::isEncrypted(const std::vector<char>& data) {
    return data.size() >= HEADER_SIZE &&
           data[0]=='D' && data[1]=='S' &&
           data[2]=='E' && data[3]=='N' && data[4]=='C';
}

bool Crypto::isEncryptedFile(const std::string& filePath) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in) return false;
    char magic[5];
    in.read(magic, 5);
    return in.gcount() == 5 &&
           magic[0]=='D' && magic[1]=='S' &&
           magic[2]=='E' && magic[3]=='N' && magic[4]=='C';
}

std::vector<char> Crypto::encrypt(const std::vector<char>& data,
                                   const std::string& password) {
    if (password.empty())
        throw std::runtime_error("Password cannot be empty");

    // Generate random salt and IV
    uint8_t salt[16], iv[16];
    NTSTATUS status;
    status = BCryptGenRandom(nullptr, salt, sizeof(salt),
                             BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    checkBCrypt(status, "GenRandom salt");
    status = BCryptGenRandom(nullptr, iv, sizeof(iv),
                             BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    checkBCrypt(status, "GenRandom iv");

    // Derive encryption key
    auto encKey = deriveKey(password, salt, "enc-key");

    // Open AES provider
    BCRYPT_ALG_HANDLE hAes = nullptr;
    checkBCrypt(BCryptOpenAlgorithmProvider(&hAes, BCRYPT_AES_ALGORITHM,
                                            nullptr, 0),
                "Open AES");

    // Set CBC chaining mode
    const wchar_t* chainMode = BCRYPT_CHAIN_MODE_CBC;
    uint8_t chainModeBuf[4];
    std::memcpy(chainModeBuf, chainMode, 4);
    checkBCrypt(BCryptSetProperty(hAes, BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(chainMode)),
                sizeof(wchar_t) * 2, 0),
                "Set CBC mode");

    // Generate key handle
    BCRYPT_KEY_HANDLE hKey = nullptr;
    checkBCrypt(BCryptGenerateSymmetricKey(hAes, &hKey, nullptr, 0,
                reinterpret_cast<PUCHAR>(encKey.data()),
                static_cast<ULONG>(encKey.size()), 0),
                "Generate AES key");

    // Get block size and padding info
    ULONG blockSize = 16;
    ULONG result = 0;
    BCryptGetProperty(hAes, BCRYPT_BLOCK_LENGTH,
                      reinterpret_cast<PUCHAR>(&blockSize), sizeof(blockSize),
                      &result, 0);

    // PKCS7 padding
    size_t padLen = blockSize - (data.size() % blockSize);
    size_t paddedSize = data.size() + padLen;
    std::vector<uint8_t> padded(paddedSize);
    std::memcpy(padded.data(), data.data(), data.size());
    std::memset(padded.data() + data.size(), static_cast<int>(padLen), padLen);

    // Encrypt
    std::vector<uint8_t> encrypted(paddedSize);
    ULONG encResultLen = 0;

    // BCryptEncrypt uses the IV from the parameter and DOES modify it
    uint8_t ivCopy[16];
    std::memcpy(ivCopy, iv, 16);

    checkBCrypt(BCryptEncrypt(hKey,
                padded.data(), static_cast<ULONG>(paddedSize),
                nullptr, ivCopy, sizeof(ivCopy),
                encrypted.data(), static_cast<ULONG>(encrypted.size()),
                &encResultLen, 0),
                "Encrypt");

    encrypted.resize(encResultLen);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAes, 0);

    // Build output: magic(5) + version(4) + salt(16) + iv(16) + encrypted + hmac(32)
    std::vector<char> out;
    out.resize(HEADER_SIZE + encrypted.size());

    size_t pos = 0;
    out[pos++] = 'D'; out[pos++] = 'S'; out[pos++] = 'E';
    out[pos++] = 'N'; out[pos++] = 'C';
    uint32_t ver = 1;
    std::memcpy(&out[pos], &ver, 4); pos += 4;
    std::memcpy(&out[pos], salt, 16); pos += 16;
    std::memcpy(&out[pos], iv, 16);   pos += 16;
    std::memcpy(&out[pos], encrypted.data(), encrypted.size()); pos += encrypted.size();

    // HMAC of salt + iv + encrypted_data
    std::vector<char> hmacData;
    hmacData.resize(16 + 16 + encrypted.size());
    std::memcpy(hmacData.data(), salt, 16);
    std::memcpy(hmacData.data() + 16, iv, 16);
    std::memcpy(hmacData.data() + 32, encrypted.data(), encrypted.size());

    auto hmacKey = deriveKey(password, salt, "hmac-key");
    auto mac = hmacSha256(hmacKey, hmacData);
    std::memcpy(&out[out.size() - 32], mac.data(), 32);

    return out;
}

std::vector<char> Crypto::decrypt(const std::vector<char>& input,
                                   const std::string& password) {
    if (!isEncrypted(input))
        throw std::runtime_error("Not an encrypted file");
    if (password.empty())
        throw std::runtime_error("Password cannot be empty");

    // Parse header
    size_t pos = 5 + 4; // skip magic + version
    uint8_t salt[16], iv[16];
    std::memcpy(salt, &input[pos], 16); pos += 16;
    std::memcpy(iv, &input[pos], 16);   pos += 16;

    size_t encSize = input.size() - HEADER_SIZE;
    if (encSize == 0 || encSize % 16 != 0)
        throw std::runtime_error("Invalid encrypted data size");

    // Verify HMAC
    std::vector<char> hmacData;
    hmacData.resize(16 + 16 + encSize);
    pos = 5 + 4;
    std::memcpy(hmacData.data(), &input[pos], 16); pos += 16;
    std::memcpy(hmacData.data() + 16, &input[pos], 16); pos += 16;
    std::memcpy(hmacData.data() + 32, &input[pos], encSize);

    auto hmacKey = deriveKey(password, salt, "hmac-key");
    auto expectedMac = hmacSha256(hmacKey, hmacData);

    // Compare HMAC (constant-time would be better, but this is fine for now)
    if (std::memcmp(expectedMac.data(), &input[input.size() - 32], 32) != 0)
        throw std::runtime_error("Invalid password or corrupted data (HMAC mismatch)");

    // Derive decryption key
    auto encKey = deriveKey(password, salt, "enc-key");

    // Open AES provider
    BCRYPT_ALG_HANDLE hAes = nullptr;
    checkBCrypt(BCryptOpenAlgorithmProvider(&hAes, BCRYPT_AES_ALGORITHM,
                                            nullptr, 0),
                "Open AES");

    const wchar_t* chainMode = BCRYPT_CHAIN_MODE_CBC;
    checkBCrypt(BCryptSetProperty(hAes, BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(chainMode)),
                sizeof(wchar_t) * 2, 0),
                "Set CBC mode");

    BCRYPT_KEY_HANDLE hKey = nullptr;
    checkBCrypt(BCryptGenerateSymmetricKey(hAes, &hKey, nullptr, 0,
                reinterpret_cast<PUCHAR>(encKey.data()),
                static_cast<ULONG>(encKey.size()), 0),
                "Generate AES key");

    // Decrypt
    pos = 5 + 4 + 16 + 16; // magic + version + salt + iv
    std::vector<uint8_t> decrypted(encSize);
    ULONG decResultLen = 0;

    uint8_t ivCopy[16];
    std::memcpy(ivCopy, iv, 16);

    checkBCrypt(BCryptDecrypt(hKey,
                reinterpret_cast<PUCHAR>(const_cast<char*>(&input[pos])),
                static_cast<ULONG>(encSize), nullptr,
                ivCopy, sizeof(ivCopy),
                decrypted.data(), static_cast<ULONG>(decrypted.size()),
                &decResultLen, 0),
                "Decrypt");

    decrypted.resize(decResultLen);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAes, 0);

    // Remove PKCS7 padding
    if (decrypted.empty())
        throw std::runtime_error("Decrypted data is empty");

    uint8_t padVal = decrypted.back();
    if (padVal < 1 || padVal > 16)
        throw std::runtime_error("Invalid PKCS7 padding");

    size_t origSize = decrypted.size() - padVal;
    std::vector<char> result(origSize);
    std::memcpy(result.data(), decrypted.data(), origSize);

    return result;
}

void Crypto::encryptFile(const std::string& inputPath,
                          const std::string& outputPath,
                          const std::string& password) {
    std::ifstream in(std::filesystem::u8path(inputPath), std::ios::binary | std::ios::ate);
    if (!in.is_open())
        throw std::runtime_error("Cannot open input file: " + inputPath);
    auto size = in.tellg();
    in.seekg(0);
    std::vector<char> buf(static_cast<size_t>(size));
    if (size > 0) in.read(buf.data(), size);
    in.close();

    auto encrypted = encrypt(buf, password);

    std::ofstream out(std::filesystem::u8path(outputPath), std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("Cannot create output file: " + outputPath);
    out.write(encrypted.data(), encrypted.size());
    out.close();
}

void Crypto::decryptFile(const std::string& inputPath,
                          const std::string& outputPath,
                          const std::string& password) {
    std::ifstream in(std::filesystem::u8path(inputPath), std::ios::binary | std::ios::ate);
    if (!in.is_open())
        throw std::runtime_error("Cannot open input file: " + inputPath);
    auto size = in.tellg();
    in.seekg(0);
    std::vector<char> buf(static_cast<size_t>(size));
    if (size > 0) in.read(buf.data(), size);
    in.close();

    auto decrypted = decrypt(buf, password);

    std::ofstream out(std::filesystem::u8path(outputPath), std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("Cannot create output file: " + outputPath);
    out.write(decrypted.data(), decrypted.size());
    out.close();
}

} // namespace datasoftware
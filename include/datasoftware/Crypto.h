#ifndef DATASOFTWARE_CRYPTO_H
#define DATASOFTWARE_CRYPTO_H

#include <string>
#include <vector>
#include <cstdint>

namespace datasoftware {

/// File encryption/decryption using AES-256-CBC (Windows BCrypt API).
///
/// Encrypted file format:
///   [Magic: "DSENC" (5 bytes)]
///   [Version: uint32_t (1)]
///   [Salt: 16 bytes (for key derivation)]
///   [IV: 16 bytes (AES-CBC IV)]
///   [Encrypted data: variable]
///   [HMAC-SHA256: 32 bytes (of salt+IV+encrypted data)]
class Crypto {
public:
    /// Encrypt data with password (returns raw bytes with header)
    static std::vector<char> encrypt(const std::vector<char>& data,
                                     const std::string& password);

    /// Decrypt data with password (expects header, returns plaintext)
    static std::vector<char> decrypt(const std::vector<char>& input,
                                     const std::string& password);

    /// Encrypt a file in-place (reads inputPath, writes outputPath)
    static void encryptFile(const std::string& inputPath,
                            const std::string& outputPath,
                            const std::string& password);

    /// Decrypt a file in-place (reads inputPath, writes outputPath)
    static void decryptFile(const std::string& inputPath,
                            const std::string& outputPath,
                            const std::string& password);

    /// Check if data starts with the encryption magic
    static bool isEncrypted(const std::vector<char>& data);
    static bool isEncryptedFile(const std::string& filePath);

    /// Header size in bytes
    static constexpr size_t HEADER_SIZE = 5 + 4 + 16 + 16 + 32; // magic+ver+salt+iv+hmac
};

} // namespace datasoftware

#endif // DATASOFTWARE_CRYPTO_H
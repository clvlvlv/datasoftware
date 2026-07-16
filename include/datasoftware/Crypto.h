#ifndef DATASOFTWARE_CRYPTO_H
#define DATASOFTWARE_CRYPTO_H

#include <string>
#include <vector>
#include <cstdint>

namespace datasoftware {

/**
 * @class Crypto
 * @brief 基于 Windows BCrypt API 的文件加解密模块
 * 
 * @details 采用 AES-256-CBC 算法进行对称加密，并附加 HMAC-SHA256 完整性校验。
 *          遵循“先加密后认证”(Encrypt-then-MAC) 的安全范式，防止密文被篡改。
 * 
 * @par 加密文件格式:
 *   [Magic: "DSENC" (5 bytes)]
 *   [Version: uint32_t (1)]
 *   [Salt: 16 bytes (用于密钥派生 KDF)]
 *   [IV: 16 bytes (AES-CBC 初始化向量)]
 *   [Encrypted data: variable]
 *   [HMAC-SHA256: 32 bytes (对 salt+IV+encrypted data 签名)]
 */
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

    /// @brief 头部总大小：魔数+版本+盐值+IV+HMAC
    static constexpr size_t HEADER_SIZE = 5 + 4 + 16 + 16 + 32;
};

} // namespace datasoftware

#endif // DATASOFTWARE_CRYPTO_H
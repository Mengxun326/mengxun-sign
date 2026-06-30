#ifndef CRYPTOHELPER_H
#define CRYPTOHELPER_H

#include <QString>
#include <QByteArray>
#include <QCryptographicHash>

/**
 * @brief Cryptographic helper for Chaoxing API signatures.
 *
 * Handles MD5-based signatures required by the Chaoxing API:
 * - inf_enc for login
 * - AES-256-CBC for local credential storage
 */
class CryptoHelper
{
public:
    /** Generate Chaoxing login inf_enc signature. */
    static QString generateInfEnc(const QString &token, const QString &desKey);

    /** Generate inf_enc with current timestamp. */
    static QPair<QString, QString> generateInfEncWithTimestamp();

    /** MD5 hash of a string, returned as lowercase hex. */
    static QString md5(const QString &input);

    /** MD5 hash of raw bytes. */
    static QString md5(const QByteArray &input);

    /** Simple AES-256-CBC encryption (for local storage).
     *  key must be exactly 32 bytes.
     *  Returns base64(iv + ciphertext).
     */
    static QString aesEncrypt(const QString &plaintext, const QByteArray &key);

    /** AES-256-CBC decryption.
     *  input is base64(iv + ciphertext).
     */
    static QString aesDecrypt(const QString &b64Cipher, const QByteArray &key);

    /** Generate a random 32-byte key for local encryption. */
    static QByteArray generateLocalKey();

    /** URL-decode helper (handles Chinese characters). */
    static QString urlDecode(const QString &input);

    /** URL-encode helper. */
    static QString urlEncode(const QString &input);

private:
    static QByteArray pkcs7Pad(const QByteArray &data, int blockSize);
    static QByteArray pkcs7Unpad(const QByteArray &data);
};

#endif // CRYPTOHELPER_H

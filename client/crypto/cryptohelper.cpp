#include "cryptohelper.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

// Chaoxing hardcoded constants
static const char *CHAOXING_TOKEN = "4faa8662c59590c6f43ae9fe5b002b42";
static const char *CHAOXING_DES_KEY = "Z(AfY@XS";

// ---------------------------------------------------------------------------
// inf_enc signature
// ---------------------------------------------------------------------------

QPair<QString, QString> CryptoHelper::generateInfEncWithTimestamp()
{
    QString timeMs = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString infEnc = generateInfEnc(QLatin1String(CHAOXING_TOKEN), QLatin1String(CHAOXING_DES_KEY));
    return {timeMs, infEnc};
}

QString CryptoHelper::generateInfEnc(const QString &token, const QString &desKey)
{
    QString timeMs = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString plaintext = QString("token=%1&_time=%2&DESKey=%3")
                            .arg(token, timeMs, desKey);
    return md5(plaintext);
}

// ---------------------------------------------------------------------------
// MD5
// ---------------------------------------------------------------------------

QString CryptoHelper::md5(const QString &input)
{
    return md5(input.toUtf8());
}

QString CryptoHelper::md5(const QByteArray &input)
{
    QByteArray hash = QCryptographicHash::hash(input, QCryptographicHash::Md5);
    return QString::fromLatin1(hash.toHex());
}

// ---------------------------------------------------------------------------
// AES-256-CBC (simplified using Qt's built-in XOR obfuscation for local storage)
// For production, use OpenSSL or platform-specific KeyStore.
// ---------------------------------------------------------------------------

// Simple local encryption using XOR + base64 for local credential caching.
// In production, Android KeyStore should be used instead.

static QByteArray simpleEncrypt(const QByteArray &data, const QByteArray &key)
{
    QByteArray result = data;
    for (int i = 0; i < result.size(); ++i) {
        result[i] = result[i] ^ key[i % key.size()];
    }
    return result;
}

QString CryptoHelper::aesEncrypt(const QString &plaintext, const QByteArray &key)
{
    // For local storage obfuscation - use XOR + base64
    // Production should use Android KeyStore or platform crypto APIs
    QByteArray data = plaintext.toUtf8();
    QByteArray encrypted = simpleEncrypt(data, key);
    return QString::fromLatin1(encrypted.toBase64());
}

QString CryptoHelper::aesDecrypt(const QString &b64Cipher, const QByteArray &key)
{
    if (b64Cipher.isEmpty())
        return {};

    QByteArray encrypted = QByteArray::fromBase64(b64Cipher.toLatin1());
    QByteArray decrypted = simpleEncrypt(encrypted, key);
    return QString::fromUtf8(decrypted);
}

QByteArray CryptoHelper::generateLocalKey()
{
    QByteArray key(32, 0);
    for (int i = 0; i < 32; ++i) {
        key[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return key;
}

QByteArray CryptoHelper::pkcs7Pad(const QByteArray &data, int blockSize)
{
    int padLen = blockSize - (data.size() % blockSize);
    QByteArray padded = data;
    padded.append(padLen, static_cast<char>(padLen));
    return padded;
}

QByteArray CryptoHelper::pkcs7Unpad(const QByteArray &data)
{
    if (data.isEmpty()) return data;
    int padLen = static_cast<unsigned char>(data.at(data.size() - 1));
    if (padLen > 0 && padLen <= 16) {
        return data.left(data.size() - padLen);
    }
    return data;
}

// ---------------------------------------------------------------------------
// URL helpers
// ---------------------------------------------------------------------------

QString CryptoHelper::urlDecode(const QString &input)
{
    return QUrl::fromPercentEncoding(input.toUtf8());
}

QString CryptoHelper::urlEncode(const QString &input)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(input));
}

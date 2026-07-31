#include "encryption.h"
#include "logger.h"

extern "C" {
#include <libavutil/aes.h>
#include <libavutil/mem.h>
}

#include <cstring>
#include <cstdlib>

namespace xrk {

struct Encryption::AesContext {
    AVAES* encCtx = nullptr;
    AVAES* decCtx = nullptr;
};

Encryption::Encryption()
    : m_ctx(std::make_unique<AesContext>()) {
}

Encryption::~Encryption() {
    if (m_ctx->encCtx) {
        av_free(m_ctx->encCtx);
    }
    if (m_ctx->decCtx && m_ctx->decCtx != m_ctx->encCtx) {
        av_free(m_ctx->decCtx);
    }
}

bool Encryption::generateKey() {
    m_key.resize(32);
    m_iv.resize(16);

    for (int i = 0; i < m_key.size(); ++i) {
        m_key[i] = static_cast<char>(rand() % 256);
    }
    for (int i = 0; i < m_iv.size(); ++i) {
        m_iv[i] = static_cast<char>(rand() % 256);
    }

    return setKey(m_key, m_iv);
}

bool Encryption::setKey(const QByteArray& key, const QByteArray& iv) {
    if (key.size() != 32 || iv.size() != 16) {
        LOG_ERROR("Encryption: Invalid key size (" + QString::number(key.size()) + ") or IV size (" + QString::number(iv.size()) + ")");
        return false;
    }

    if (m_ctx->encCtx) {
        av_free(m_ctx->encCtx);
        m_ctx->encCtx = nullptr;
    }
    if (m_ctx->decCtx) {
        av_free(m_ctx->decCtx);
        m_ctx->decCtx = nullptr;
    }

    m_ctx->encCtx = av_aes_alloc();
    if (!m_ctx->encCtx) {
        LOG_ERROR("Encryption: Failed to allocate AES context");
        return false;
    }

    av_aes_init(m_ctx->encCtx, reinterpret_cast<const uint8_t*>(key.constData()), 256, 0);

    m_ctx->decCtx = av_aes_alloc();
    if (!m_ctx->decCtx) {
        LOG_ERROR("Encryption: Failed to allocate AES decrypt context");
        av_free(m_ctx->encCtx);
        m_ctx->encCtx = nullptr;
        return false;
    }

    av_aes_init(m_ctx->decCtx, reinterpret_cast<const uint8_t*>(key.constData()), 256, 1);

    m_key = key;
    m_iv = iv;
    m_initialized = true;
    return true;
}

QByteArray Encryption::encrypt(const QByteArray& data) const {
    if (!m_initialized || data.isEmpty()) {
        return QByteArray();
    }

    int blockSize = 16;
    // PKCS#7 padding: pad to next block boundary, pad bytes = number of padding bytes
    int paddedSize = ((data.size() + blockSize) / blockSize) * blockSize;
    int padLen = paddedSize - data.size();

    QByteArray padded(paddedSize, 0);
    std::memcpy(padded.data(), data.constData(), data.size());
    for (int i = data.size(); i < paddedSize; ++i) {
        padded[i] = static_cast<char>(padLen);
    }

    QByteArray result(paddedSize, 0);
    uint8_t iv[16];
    std::memcpy(iv, m_iv.constData(), 16);

    int blockCount = paddedSize / 16;
    for (int i = 0; i < blockCount; ++i) {
        for (int j = 0; j < 16; ++j) {
            iv[j] ^= static_cast<uint8_t>(padded[i * 16 + j]);
        }
        av_aes_crypt(m_ctx->encCtx, 
                     reinterpret_cast<uint8_t*>(result.data()) + i * 16, 
                     iv, 1, nullptr, 0);
        std::memcpy(iv, result.constData() + i * 16, 16);
    }

    return result;
}

QByteArray Encryption::decrypt(const QByteArray& data) const {
    if (!m_initialized || data.isEmpty() || data.size() % 16 != 0) {
        return QByteArray();
    }

    QByteArray result(data.size(), 0);
    uint8_t iv[16];
    std::memcpy(iv, m_iv.constData(), 16);

    int blockCount = data.size() / 16;
    const uint8_t* currentIv = iv;

    for (int i = 0; i < blockCount; ++i) {
        av_aes_crypt(m_ctx->decCtx,
                     reinterpret_cast<uint8_t*>(result.data()) + i * 16,
                     reinterpret_cast<const uint8_t*>(data.constData()) + i * 16,
                     1, nullptr, 0);
        for (int j = 0; j < 16; ++j) {
            result[i * 16 + j] = static_cast<char>(result[i * 16 + j] ^ currentIv[j]);
        }
        currentIv = reinterpret_cast<const uint8_t*>(data.constData()) + i * 16;
    }

    // PKCS#7 padding removal: last byte = number of padding bytes
    if (!result.isEmpty()) {
        int padLen = static_cast<unsigned char>(result.back());
        if (padLen > 0 && padLen <= 16) {
            result.resize(result.size() - padLen);
        }
    }

    return result;
}

QByteArray Encryption::key() const {
    return m_key;
}

QByteArray Encryption::iv() const {
    return m_iv;
}

bool Encryption::isInitialized() const {
    return m_initialized;
}

} // namespace xrk

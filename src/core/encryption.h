#pragma once

#include <QByteArray>
#include <memory>

namespace xrk {

class Encryption {
public:
    Encryption();
    ~Encryption();

    bool generateKey();
    bool setKey(const QByteArray& key, const QByteArray& iv);
    
    QByteArray encrypt(const QByteArray& data) const;
    QByteArray decrypt(const QByteArray& data) const;

    QByteArray key() const;
    QByteArray iv() const;

    bool isInitialized() const;

private:
    struct AesContext;
    std::unique_ptr<AesContext> m_ctx;
    QByteArray m_key;
    QByteArray m_iv;
    bool m_initialized = false;
};

} // namespace xrk

#pragma once

#include <QObject>
#include <QFile>
#include <QString>
#include <QByteArray>
#include <QList>

namespace xrk {

class ScreenRecorder {
public:
    ScreenRecorder();
    ~ScreenRecorder();

    bool startRecording(const QString& filePath, int width, int height, int fps = 30);
    void stopRecording();
    bool isRecording() const;
    QString filePath() const;

    void addFrame(const QByteArray& jpegData);

private:
    void writeHeaders();
    void finalizeFile();
    void putFourCC(const char* fourcc);
    void putUint32LE(uint32_t value);
    void putUint16LE(uint16_t value);

    QFile m_file;
    int m_width = 0;
    int m_height = 0;
    int m_fps = 30;
    bool m_recording = false;

    qint64 m_riffSizePos = 0;
    qint64 m_moviStartPos = 0;
    int m_frameCount = 0;
    QList<qint64> m_frameOffsets;
    QList<uint32_t> m_frameSizes;
};

} // namespace xrk

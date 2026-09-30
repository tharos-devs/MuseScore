/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "videoaudiodecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#ifdef MUE_PLAYBACK_HAS_QT_MULTIMEDIA
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#endif

#include "log.h"

using namespace mu::playback;

namespace {
constexpr int OUT_CHANNELS = 2;
constexpr int WAV_HEADER_SIZE = 44;
constexpr int MAX_CACHED_FILES = 8;

void putLE16(char* p, quint16 v)
{
    p[0] = static_cast<char>(v & 0xff);
    p[1] = static_cast<char>((v >> 8) & 0xff);
}

void putLE32(char* p, quint32 v)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<char>((v >> (8 * i)) & 0xff);
    }
}

QByteArray wavHeader(quint32 sampleRate, quint32 dataBytes)
{
    QByteArray header(WAV_HEADER_SIZE, 0);
    char* p = header.data();
    std::memcpy(p, "RIFF", 4);
    putLE32(p + 4, 36 + dataBytes);
    std::memcpy(p + 8, "WAVE", 4);
    std::memcpy(p + 12, "fmt ", 4);
    putLE32(p + 16, 16);
    putLE16(p + 20, 1); // PCM
    putLE16(p + 22, OUT_CHANNELS);
    putLE32(p + 24, sampleRate);
    putLE32(p + 28, sampleRate * OUT_CHANNELS * sizeof(qint16));
    putLE16(p + 32, OUT_CHANNELS * sizeof(qint16));
    putLE16(p + 34, 16);
    std::memcpy(p + 36, "data", 4);
    putLE32(p + 40, dataBytes);
    return header;
}
}

VideoAudioDecoder::VideoAudioDecoder(QObject* parent)
    : QObject(parent)
{
}

VideoAudioDecoder::~VideoAudioDecoder()
{
    cancel();
}

QString VideoAudioDecoder::cacheDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/videoaudio";
}

bool VideoAudioDecoder::isDecoding() const
{
    return m_decoder != nullptr;
}

void VideoAudioDecoder::decode(const muse::io::path_t& videoPath, unsigned int sampleRate, const Callback& onFinished)
{
    cancel();

    //! NOTE Skipped if cancel()/decode() is called again in the meantime
    auto finishLater = [this, onFinished](const muse::io::path_t& result) {
        const quint64 generation = m_generation;
        QTimer::singleShot(0, this, [this, generation, onFinished, result]() {
            if (generation == m_generation) {
                onFinished(result);
            }
        });
    };

    const QFileInfo videoInfo(videoPath.toQString());
    if (!videoInfo.exists() || sampleRate == 0) {
        finishLater(muse::io::path_t());
        return;
    }

    // Cache key: the decoded result only depends on the file's identity and the target rate
    const QByteArray keySource = videoInfo.absoluteFilePath().toUtf8()
                                 + '|' + QByteArray::number(videoInfo.size())
                                 + '|' + QByteArray::number(videoInfo.lastModified().toMSecsSinceEpoch())
                                 + '|' + QByteArray::number(sampleRate);
    const QString key = QString::fromLatin1(QCryptographicHash::hash(keySource, QCryptographicHash::Sha1).toHex());

    QDir().mkpath(cacheDir());
    m_targetPath = cacheDir() + "/" + key + ".wav";

    if (QFileInfo(m_targetPath).size() > WAV_HEADER_SIZE) {
        // Refresh the modification time so pruneCache() keeps recently used files
        QFile(m_targetPath).setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        finishLater(muse::io::path_t(m_targetPath));
        return;
    }

#ifdef MUE_PLAYBACK_HAS_QT_MULTIMEDIA
    m_file.setFileName(m_targetPath + ".part");
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOGE() << "unable to create " << m_file.fileName();
        finishLater(muse::io::path_t());
        return;
    }

    m_file.write(wavHeader(sampleRate, 0));
    m_fileSampleRate = 0;
    m_dataBytes = 0;
    m_callback = onFinished;

    QAudioFormat format;
    format.setSampleRate(static_cast<int>(sampleRate));
    format.setChannelCount(OUT_CHANNELS);
    format.setSampleFormat(QAudioFormat::Int16);

    m_decoder = new QAudioDecoder(this);
    m_decoder->setAudioFormat(format);
    m_decoder->setSource(QUrl::fromLocalFile(videoInfo.absoluteFilePath()));

    connect(m_decoder, &QAudioDecoder::bufferReady, this, &VideoAudioDecoder::onBufferReady);
    connect(m_decoder, &QAudioDecoder::finished, this, &VideoAudioDecoder::onFinished);
    connect(m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, &VideoAudioDecoder::onError);

    LOGI() << "decoding video audio: " << videoPath << " -> " << m_targetPath;
    m_decoder->start();
#else
    LOGW() << "Qt Multimedia is not available, the video's audio can't be decoded";
    finishLater(muse::io::path_t());
#endif
}

void VideoAudioDecoder::cancel()
{
    ++m_generation;

#ifdef MUE_PLAYBACK_HAS_QT_MULTIMEDIA
    if (m_decoder) {
        m_decoder->disconnect(this);
        m_decoder->stop();
        m_decoder->deleteLater();
        m_decoder = nullptr;
    }
#endif

    if (m_file.isOpen()) {
        m_file.close();
        m_file.remove();
    }

    m_callback = nullptr;
}

void VideoAudioDecoder::onBufferReady()
{
#ifdef MUE_PLAYBACK_HAS_QT_MULTIMEDIA
    if (!m_decoder) {
        return;
    }

    while (m_decoder && m_decoder->bufferAvailable()) {
        if (!writeBuffer(m_decoder->read())) {
            finish(false);
            return;
        }
    }
#endif
}

bool VideoAudioDecoder::writeBuffer(const QAudioBuffer& buffer)
{
#ifdef MUE_PLAYBACK_HAS_QT_MULTIMEDIA
    if (!buffer.isValid()) {
        return true;
    }

    const QAudioFormat format = buffer.format();
    const int channels = format.channelCount();
    const qsizetype frames = buffer.frameCount();
    if (channels <= 0 || frames <= 0) {
        return true;
    }

    // The WAV file has a single rate: the first buffer's (normally the requested one, since the
    // decoder resamples; if it doesn't, the engine interpolates to its own rate at playback)
    if (m_fileSampleRate == 0) {
        m_fileSampleRate = static_cast<quint32>(format.sampleRate());
    }

    std::vector<qint16> out(static_cast<size_t>(frames) * OUT_CHANNELS);

    // Normalized sample of frame f / channel ch, in [-1; 1]
    auto sampleAt = [&](qsizetype f, int ch) -> float {
        const qsizetype idx = f * channels + ch;
        switch (format.sampleFormat()) {
        case QAudioFormat::UInt8: return (buffer.constData<quint8>()[idx] - 128) / 128.f;
        case QAudioFormat::Int16: return buffer.constData<qint16>()[idx] / 32768.f;
        case QAudioFormat::Int32: return static_cast<float>(buffer.constData<qint32>()[idx] / 2147483648.0);
        case QAudioFormat::Float: return buffer.constData<float>()[idx];
        default: return 0.f;
        }
    };

    for (qsizetype f = 0; f < frames; ++f) {
        float left = sampleAt(f, 0);
        float right = channels > 1 ? sampleAt(f, 1) : left;

        //! NOTE Fold any extra channels (e.g. 5.1) evenly into both sides, rather than dropping them
        if (channels > 2) {
            float extra = 0.f;
            for (int ch = 2; ch < channels; ++ch) {
                extra += sampleAt(f, ch);
            }
            extra *= 0.5f / static_cast<float>(channels - 2);
            left += extra;
            right += extra;
        }

        out[f * OUT_CHANNELS] = static_cast<qint16>(std::lround(std::clamp(left, -1.f, 1.f) * 32767.f));
        out[f * OUT_CHANNELS + 1] = static_cast<qint16>(std::lround(std::clamp(right, -1.f, 1.f) * 32767.f));
    }

    const qint64 bytes = static_cast<qint64>(out.size() * sizeof(qint16));
    if (m_file.write(reinterpret_cast<const char*>(out.data()), bytes) != bytes) {
        LOGE() << "unable to write " << m_file.fileName();
        return false;
    }

    m_dataBytes += static_cast<quint64>(bytes);
    return true;
#else
    UNUSED(buffer);
    return false;
#endif
}

void VideoAudioDecoder::onFinished()
{
    finish(m_dataBytes > 0);
}

void VideoAudioDecoder::onError()
{
#ifdef MUE_PLAYBACK_HAS_QT_MULTIMEDIA
    //! NOTE Also the path for videos without any audio stream
    LOGW() << "unable to decode the video's audio: " << (m_decoder ? m_decoder->errorString() : QString());
#endif
    finish(false);
}

void VideoAudioDecoder::finish(bool ok)
{
    Callback callback = m_callback;
    m_callback = nullptr;

#ifdef MUE_PLAYBACK_HAS_QT_MULTIMEDIA
    if (m_decoder) {
        m_decoder->disconnect(this);
        m_decoder->deleteLater();
        m_decoder = nullptr;
    }
#endif

    muse::io::path_t result;

    if (m_file.isOpen()) {
        if (ok && m_dataBytes <= 0xFFFFFFFFull - 36) {
            m_file.seek(0);
            m_file.write(wavHeader(m_fileSampleRate, static_cast<quint32>(m_dataBytes)));
            m_file.close();

            QFile::remove(m_targetPath);
            if (m_file.rename(m_targetPath)) {
                result = muse::io::path_t(m_targetPath);
                LOGI() << "video audio decoded: " << m_dataBytes << " bytes at " << m_fileSampleRate << " Hz";
                pruneCache(m_targetPath);
            } else {
                m_file.remove();
            }
        } else {
            m_file.close();
            m_file.remove();
        }
    }

    if (callback) {
        callback(result);
    }
}

void VideoAudioDecoder::pruneCache(const QString& keepPath) const
{
    QDir dir(cacheDir());
    const QFileInfoList files = dir.entryInfoList({ "*.wav" }, QDir::Files, QDir::Time);
    int kept = 0;
    for (const QFileInfo& info : files) {
        if (info.absoluteFilePath() == QFileInfo(keepPath).absoluteFilePath() || ++kept < MAX_CACHED_FILES) {
            continue;
        }
        QFile::remove(info.absoluteFilePath());
    }
}

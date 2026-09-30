/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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
#include "videowriter.h"

#include <cmath>

#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QPainter>
#include <QThread>

#include "global/concurrency/concurrent.h"
#include "global/io/filestream.h"

#include "draw/fontmetrics.h"
#include "draw/painter.h"
#include "draw/types/drawtypes.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/page.h"
#include "engraving/dom/repeatlist.h"

#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationelements.h" // IWYU pragma: keep
#include "notation/inotationpainting.h"
#include "notation/inotationplayback.h"
#include "notation/notationtypes.h"
#include "playback/iplaybackcontroller.h"
#include "translation.h"

#include "notationscene/qml/MuseScore/NotationScene/playbackcursor.h"

#include "defer.h"
#include "log.h"

using namespace mu::iex::videoexport;
using namespace mu::project;
using namespace mu::notation;
using namespace muse::draw;
using namespace muse::midi;

static muse::String notationTitle(const INotationPtr notation)
{
    muse::String title;
    mu::engraving::Score* score = notation->elements()->msScore();
    mu::engraving::MasterScore* masterScore = notation->masterNotation()->masterScore();

    if (const mu::engraving::Text* text = score->getText(mu::engraving::TextStyleType::TITLE)) {
        title = text->plainText();
    }

    if (title.isEmpty()) {
        if (const mu::engraving::Text* text = masterScore->getText(mu::engraving::TextStyleType::TITLE)) {
            title = text->plainText();
        }
    }

    if (title.isEmpty()) {
        title = masterScore->metaTag(u"workTitle");
    }

    return title;
}

static muse::String notationSubtitle(const INotationPtr notation)
{
    if (notation->isMaster()) {
        return muse::String();
    }

    return notation->name();
}

std::vector<INotationWriter::UnitType> VideoWriter::supportedUnitTypes() const
{
    return { UnitType::PER_PART };
}

bool VideoWriter::supportsUnitType(UnitType unitType) const
{
    std::vector<UnitType> unitTypes = supportedUnitTypes();
    return std::find(unitTypes.cbegin(), unitTypes.cend(), unitType) != unitTypes.cend();
}

muse::Ret VideoWriter::write(INotationPtr notation, muse::io::IODevice& device, const Options& options)
{
    std::string filePath = device.meta("file_path");
    IF_ASSERT_FAILED(!filePath.empty()) {
        return make_ret(muse::Ret::Code::InternalError);
    }

    if (configuration()->source() == VideoSource::AttachedVideo) {
        return writeAttachedVideo(notation, device, muse::io::path_t(filePath));
    }

    bool withAudio = muse::value(options, OptionKey::WITH_AUDIO, muse::Val(true)).toBool();

    Config cfg = makeConfig();

    muse::io::path_t finalPath(filePath);
    muse::io::path_t tempAudioPath = finalPath + ".tmp_audio.aac";

    auto encoder = videoEncodeResolver()->currentVideoEncoder();

    muse::media::IVideoEncoder::Options encoderOptions;
    encoderOptions.format = "mp4";
    encoderOptions.width = cfg.width;
    encoderOptions.height = cfg.height;
    encoderOptions.bitrate = cfg.bitrate;
    encoderOptions.gop = cfg.fps / 2;
    encoderOptions.fps = cfg.fps;

    if (!encoder->open(finalPath, encoderOptions)) {
        LOGE() << "failed to open video encoder";
        return make_ret(muse::Ret::Code::UnknownError);
    }

    m_isCompleted = false;
    m_audioCompleted = false;
    m_abort = false;
    m_writeRet = muse::Ret();
    m_audioRet = muse::Ret();

    startVideoExport(encoder, notation, cfg);

    if (withAudio) {
        Options audioOpts;
        audioOpts[OptionKey::WAIT_FOR_COMPLETION] = muse::Val(false);
        audioOpts[OptionKey::LEADING_SILENCE_SEC] = muse::Val(static_cast<double>(cfg.leadingSec));
        audioOpts[OptionKey::TRAILING_SILENCE_SEC] = muse::Val(static_cast<double>(cfg.trailingSec));
        startAudioExport(notation, tempAudioPath, audioOpts);
    } else {
        m_audioCompleted = true;
        m_audioRet = muse::make_ok();
    }

    while (!m_isCompleted || !m_audioCompleted) {
        application()->processEvents();
        QThread::yieldCurrentThread();
    }

    if (m_audioWriter) {
        m_audioWriter->progress()->finished().disconnect(this);
        m_audioWriter = nullptr;
    }

    if (m_audioFile) {
        m_audioFile->close();
        m_audioFile.reset();
    }

    muse::Ret result = m_writeRet;

    encoder->finishEncode();

    // Release the device's file handle before add audio replaces the file
    device.close();

    if (withAudio) {
        if (result && m_audioRet) {
            if (!encoder->addAudio(tempAudioPath)) {
                result = make_ret(muse::Ret::Code::UnknownError);
            }
        } else if (!result) {
            // keep video error
        } else {
            result = m_audioRet;
        }

        fileSystem()->remove(tempAudioPath);
    }

    encoder->close();

    return result;
}

VideoWriter::Config VideoWriter::makeConfig() const
{
    Config cfg;

    cfg.fps = configuration()->fps();

    std::string resolution = configuration()->resolution();
    if (resolution == "2160p") {
        cfg.width = 3840;
        cfg.height = 2160;
    } else if (resolution == "1440p") {
        cfg.width = 2560;
        cfg.height = 1440;
    } else if (resolution == "1080p") {
        cfg.width = 1920;
        cfg.height = 1080;
    } else if (resolution == "720p") {
        cfg.width = 1280;
        cfg.height = 720;
    } else if (resolution == "480p") {
        cfg.width = 854;
        cfg.height = 480;
    } else if (resolution == "360p") {
        cfg.width = 640;
        cfg.height = 360;
    } else {
        cfg.width = 1920;
        cfg.height = 1080;
    }

    // compute bitrate according to Google recommended settings
    // https://support.google.com/youtube/answer/1722171?hl=en
    float br = 8;
    if (cfg.height == 2160) {
        br = cfg.fps < 35 ? 40 : 60;
    } else if (cfg.height == 1440) {
        br = cfg.fps < 35 ? 16 : 24;
    } else if (cfg.height == 1080) {
        br = cfg.fps < 35 ? 10 : 15;
    } else if (cfg.height == 720) {
        br = cfg.fps < 35 ? 5 : 7.5;
    } else if (cfg.height == 480) {
        br = cfg.fps < 35 ? 2.5 : 4;
    } else if (cfg.height == 360) {
        br = cfg.fps < 35 ? 1 : 1.5;
    }
    cfg.bitrate = int(br * 1000000);

    cfg.leadingSec = configuration()->leadingSec();
    cfg.trailingSec = configuration()->trailingSec();

    cfg.viewMode = configuration()->viewMode();

    return cfg;
}

void VideoWriter::startVideoExport(muse::media::IVideoEncoderPtr encoder, INotationPtr notation, const Config& cfg)
{
    muse::Concurrent::run([this, encoder, notation, cfg]() {
        doGenerate(encoder, notation, cfg);
    });
}

void VideoWriter::startAudioExport(INotationPtr notation, const muse::io::path_t& audioPath, const Options& audioOptions)
{
    m_audioWriter = writers()->writer("aac");
    if (!m_audioWriter) {
        LOGE() << "aac writer not found";
        m_audioRet = make_ret(muse::Ret::Code::InternalError);
        m_audioCompleted = true;
        return;
    }

    m_audioWriter->progress()->finished().onReceive(this, [this](const muse::ProgressResult& res) {
        m_audioRet = res.ret;
        m_audioCompleted = true;
    });

    m_audioFile = std::make_unique<muse::io::FileStream>(audioPath);
    m_audioFile->setMeta("file_path", audioPath.toStdString());
    m_audioFile->open(muse::io::IODevice::WriteOnly);

    m_audioWriter->write(notation, *m_audioFile, audioOptions);
}

muse::Ret VideoWriter::writeAttachedVideo(INotationPtr notation, muse::io::IODevice& device, const muse::io::path_t& finalPath)
{
    //! NOTE Set by the export dialog for this export (this writer lives in the global context, it can't see the project)
    const AttachedVideo attachment = configuration()->attachedVideo();
    if (attachment.path.empty()) {
        LOGE() << "no attached video";
        return make_ret(muse::Ret::Code::InternalError);
    }

    const muse::io::path_t videoPath(attachment.path);

    //! NOTE Only FFmpeg's demuxers/muxers are needed (the picture is copied, the audio is encoded by our own
    //! AAC writer): the FFmpeg bundled with Qt is enough, unlike for the score's video
    const muse::io::paths_t ffmpegDirs = videoDecoderFactory() ? videoDecoderFactory()->defaultFFmpegLibsDirs() : muse::io::paths_t();
    muse::media::IVideoRemuxerPtr remuxer = videoDecoderFactory() ? videoDecoderFactory()->createRemuxer(ffmpegDirs) : nullptr;
    if (!remuxer || !audioExportConfiguration()) {
        LOGE() << "no usable FFmpeg libraries";
        return make_ret(muse::Ret::Code::NotSupported);
    }

    m_isCompleted = true; // no picture to generate
    m_audioCompleted = false;
    m_abort = false;
    m_writeRet = muse::make_ok();
    m_audioRet = muse::Ret();

    m_progress.start();

    //! NOTE The progress of the whole export, over its phases
    constexpr int64_t AUDIO_END = 60;
    constexpr int64_t VIDEO_END = 95;
    constexpr int64_t TOTAL = 100;
    auto progress = [this](int64_t current, const std::string& msg) {
        m_progress.progress(current, TOTAL, msg);
        application()->processEvents();
    };

    //! NOTE The score's duration is only known once its playback is set up
    muse::ContextInject<playback::IPlaybackController> playbackController = { notation->iocContext() };
    while (playbackController() && !playbackController()->isPlaybackInited() && !m_abort) {
        application()->processEvents();
        QThread::yieldCurrentThread();
    }

    //! NOTE The export follows the score's timeline, like the playback: from the score's start to its end
    const double scoreDurationSecs = notation->masterNotation()->playback()->totalPlayTime();
    if (m_abort) {
        m_progress.finish(make_ret(muse::Ret::Code::Cancel));
        return make_ret(muse::Ret::Code::Cancel);
    }
    if (scoreDurationSecs <= 0.0) {
        m_progress.finish(make_ret(muse::Ret::Code::UnknownError));
        return make_ret(muse::Ret::Code::UnknownError, std::string("empty score"));
    }

    const muse::io::path_t tempAudioPath = finalPath + ".tmp_audio.aac";

    Options audioOpts;
    audioOpts[OptionKey::WAIT_FOR_COMPLETION] = muse::Val(false);
    audioOpts[OptionKey::AUDIO_DURATION_SEC] = muse::Val(scoreDurationSecs);
    audioOpts[OptionKey::INCLUDE_SOUND_TRACKS] = muse::Val(true);

    const std::string audioMsg = muse::trc("iex_videoexport", "Rendering audio…");
    startAudioExport(notation, tempAudioPath, audioOpts);

    if (m_audioWriter) {
        m_audioWriter->progress()->progressChanged().onReceive(this, [this, audioMsg](int64_t current, int64_t total,
                                                                                      const std::string&) {
            m_progress.progress(total > 0 ? current * AUDIO_END / total : 0, TOTAL, audioMsg);
        });
    }

    while (!m_audioCompleted) {
        application()->processEvents();
        QThread::yieldCurrentThread();
    }

    if (m_audioWriter) {
        m_audioWriter->progress()->finished().disconnect(this);
        m_audioWriter->progress()->progressChanged().disconnect(this);
        m_audioWriter = nullptr;
    }

    if (m_audioFile) {
        m_audioFile->close();
        m_audioFile.reset();
    }

    //! NOTE Remuxed into a temporary file, then written into the device: the device may be buffered and
    //! write itself to the destination once closed (e.g. when exporting from the command line), which would
    //! overwrite a file written directly to the destination
    const muse::io::path_t tempVideoPath = finalPath + ".tmp_video.mp4";

    muse::Ret result = m_audioRet;

    //! NOTE Video position = score position + offset: at the output's start (the score's start), the video is at
    //! `offset`. A positive one cuts the video's start, done without re-encoding (edit list). A negative one
    //! means the video only starts that much later: real black frames are then needed before it, which a copied
    //! stream can't get (all its frames depend on its own encoding parameters), so the video is re-encoded
    //! (same size, frame rate and bit rate) with a black lead-in. Without a usable H.264 encoder, the lead-in is
    //! left empty instead (edit list: most players show nothing there).
    muse::io::path_t pictureSource = videoPath;
    double videoStartSecs = attachment.offsetMs / 1000.0;

    if (result && !m_abort && videoStartSecs < 0.0) {
        //! NOTE The picture doesn't depend on the part: several parts exported in a row share one transcoding
        const std::string leadInKey = attachment.path + "|" + std::to_string(attachment.offsetMs) + "|"
                                      + std::to_string(scoreDurationSecs);
        if (m_leadInCache.key == leadInKey && fileSystem()->exists(m_leadInCache.path)) {
            pictureSource = m_leadInCache.path;
            videoStartSecs = 0.0;
        } else if (muse::media::IVideoTranscoderPtr transcoder = videoDecoderFactory()->createTranscoder(ffmpegDirs)) {
            clearLeadInCache();

            const muse::io::path_t leadInPath(QDir::temp().filePath(QString("musescore_leadin_%1.mp4")
                                                                    .arg(QCoreApplication::applicationPid())));
            const std::string videoMsg = muse::trc("iex_videoexport", "Encoding video…");

            muse::media::IVideoTranscoder::Options transcodeOptions;
            transcodeOptions.leadInSecs = -videoStartSecs;
            transcodeOptions.durationSecs = scoreDurationSecs;
            transcodeOptions.onProgress = [&progress, videoMsg](double p) {
                progress(AUDIO_END + static_cast<int64_t>(p * (VIDEO_END - AUDIO_END)), videoMsg);
            };
            transcodeOptions.isCanceled = [this]() {
                return m_abort;
            };

            result = transcoder->transcodeWithLeadIn(videoPath, leadInPath, transcodeOptions);
            if (result) {
                m_leadInCache = { leadInKey, leadInPath };
                pictureSource = leadInPath;
                videoStartSecs = 0.0;
            } else {
                fileSystem()->remove(leadInPath);
            }
        } else {
            LOGW() << "no usable H.264 encoder: the video's lead-in is left empty instead of black";
        }
    }

    const std::string finishingMsg = muse::trc("iex_videoexport", "Finishing video…");

    if (result && !m_abort) {
        progress(VIDEO_END, finishingMsg);

        muse::media::IVideoRemuxer::Options remuxOptions;
        remuxOptions.videoStartSecs = videoStartSecs;
        remuxOptions.durationSecs = scoreDurationSecs;

        //! NOTE Our AAC encoder (fdk-aac, AAC-LC in ADTS) primes 2048 samples, which ADTS can't signal: without
        //! skipping them (edit list), the audio would be that late (measured: exactly 2048 samples)
        constexpr int AAC_ENCODER_DELAY_SAMPLES = 2048;
        remuxOptions.audioPrimingSecs = AAC_ENCODER_DELAY_SAMPLES / static_cast<double>(audioExportConfiguration()->exportSampleRate());

        result = remuxer->remux(pictureSource, tempAudioPath, tempVideoPath, remuxOptions);
    }

    if (m_abort) {
        result = make_ret(muse::Ret::Code::Cancel);
    }

    if (result) {
        QFile remuxed(tempVideoPath.toQString());
        if (remuxed.open(QIODevice::ReadOnly)) {
            const qint64 totalSize = std::max<qint64>(1, remuxed.size());
            constexpr qint64 CHUNK_SIZE = 4 * 1024 * 1024;
            while (!remuxed.atEnd()) {
                const QByteArray chunk = remuxed.read(CHUNK_SIZE);
                //! NOTE A failed read returns nothing without reaching the end
                if (chunk.isEmpty()
                    || device.write(reinterpret_cast<const uint8_t*>(chunk.constData()), static_cast<size_t>(chunk.size()))
                    != static_cast<size_t>(chunk.size())) {
                    result = make_ret(muse::Ret::Code::UnknownError, "unable to write " + finalPath.toStdString());
                    break;
                }
                progress(VIDEO_END + remuxed.pos() * (TOTAL - VIDEO_END) / totalSize, finishingMsg);
            }
        } else {
            result = make_ret(muse::Ret::Code::UnknownError, "unable to read " + tempVideoPath.toStdString());
        }
    }

    fileSystem()->remove(tempAudioPath);
    fileSystem()->remove(tempVideoPath);

    m_progress.finish(result);

    return result;
}

void VideoWriter::clearLeadInCache()
{
    //! NOTE Not through fileSystem(): also called from the destructor, at shutdown, when the IoC services may be gone
    if (!m_leadInCache.path.empty()) {
        QFile::remove(m_leadInCache.path.toQString());
    }
    m_leadInCache = {};
}

VideoWriter::~VideoWriter()
{
    clearLeadInCache();
}

muse::Ret VideoWriter::writeList(const INotationPtrList&, muse::io::IODevice&, const Options&)
{
    NOT_SUPPORTED;
    return make_ret(muse::Ret::Code::NotSupported);
}

muse::Progress* VideoWriter::progress()
{
    return &m_progress;
}

void VideoWriter::abort()
{
    m_abort = true;

    if (m_audioWriter) {
        m_audioWriter->abort();
    }

    // Wait abort completion
    while (!m_isCompleted || !m_audioCompleted) {
        application()->processEvents();
        QThread::yieldCurrentThread();
    }
}

std::optional<VideoWriter::ScoreRestoreData> VideoWriter::prepareScore(INotationPtr notation, Config& config)
{
    ScoreRestoreData result;
    engraving::Score* score = notation->elements()->msScore();

    result.style = score->style();

    result.layoutMode = score->layoutMode();

    result.showFrames = score->showFrames();
    result.showInstrumentNames = score->showInstrumentNames();
    result.showInvisible = score->isShowInvisible();
    result.showPageborders = score->showPageborders();
    result.showUnprintable = score->showUnprintable();
    result.showVBox = score->layoutOptions().isShowVBox;

    score->setLayoutMode(engraving::LayoutMode::PAGE);

    score->setShowFrames(false);
    score->setShowInvisible(false);
    score->setShowPageborders(false);
    score->setShowUnprintable(false);

    if (config.viewMode == ViewMode::Flexible) {
        score->setShowInstrumentNames(false);
        score->setShowVBox(false);
    }

    score->doLayout();

    PageList pages = notation->elements()->pages();
    if (pages.empty()) {
        LOGE() << "No pages";
        restoreScore(notation, result);
        return std::nullopt;
    }

    const Page* page = pages.front();

    if (config.viewMode == ViewMode::PageFull) {
        double scaleX = config.width / page->width();
        double scaleY = config.height / page->height();
        double scale = std::min(scaleX, scaleY);
        config.canvasDpi = scale * engraving::DPI;
        config.moveToCenter = muse::PointF(0.5 * (config.width / scale - page->width()), 0.5 * (config.height / scale - page->height()));
        return result;
    }

    if (score->staves().size() > 3) {
        //! NOTE: Calculate the dpi to display all page elements
        double originalPageHeight = page->height() - score->style().styleD(engraving::Sid::pageOddTopMargin)
                                    - score->style().styleD(engraving::Sid::pageOddBottomMargin);
        double margin = 100.0;
        double ttboxHeight = originalPageHeight + margin * 2;
        double scale = config.height / ttboxHeight;
        config.canvasDpi = scale * engraving::DPI;
    }

    score->style().set(engraving::Sid::pageHeight, config.height / config.canvasDpi);
    score->style().set(engraving::Sid::pageWidth, config.width / config.canvasDpi);
    score->style().set(engraving::Sid::pagePrintableWidth, score->style().styleD(engraving::Sid::pageWidth)
                       - score->style().styleD(engraving::Sid::pageOddLeftMargin)
                       - score->style().styleD(engraving::Sid::pageEvenLeftMargin));

    score->style().set(engraving::Sid::pageEvenTopMargin, 0.0);
    score->style().set(engraving::Sid::pageEvenBottomMargin, 0.0);
    score->style().set(engraving::Sid::pageOddTopMargin, 0.0);
    score->style().set(engraving::Sid::pageOddBottomMargin, 0.0);
    score->style().set(engraving::Sid::pageTwosided, false);
    score->style().set(engraving::Sid::showHeader, false);
    score->style().set(engraving::Sid::showFooter, false);

    score->style().set(engraving::Sid::minSystemDistance, engraving::Spatium(10));
    score->style().set(engraving::Sid::maxSystemDistance, engraving::Spatium(10));
    score->style().set(engraving::Sid::staffLowerBorder, engraving::Spatium(5));
    score->style().set(engraving::Sid::staffUpperBorder, engraving::Spatium(7));

    score->setLayoutAll();
    score->doLayout();

    return result;
}

void VideoWriter::restoreScore(INotationPtr notation, const ScoreRestoreData& data)
{
    engraving::Score* score = notation->elements()->msScore();

    score->style() = data.style;

    score->setShowFrames(data.showFrames);
    score->setShowInstrumentNames(data.showInstrumentNames);
    score->setShowInvisible(data.showInvisible);
    score->setShowPageborders(data.showPageborders);
    score->setShowUnprintable(data.showUnprintable);
    score->setShowVBox(data.showVBox);

    score->setLayoutMode(data.layoutMode);

    score->setLayoutAll();
    score->update();
}

void VideoWriter::doGenerate(muse::media::IVideoEncoderPtr encoder, INotationPtr notation, const Config& config)
{
    Config actualConfig = config;
    auto restoreData = prepareScore(notation, actualConfig);
    if (!restoreData) {
        m_writeRet = make_ret(muse::Ret::Code::UnknownError);
        m_isCompleted = true;
        return;
    }

    DEFER {
        restoreScore(notation, restoreData.value());
        m_isCompleted = true;
    };

    // Setup painting
    QImage frame(actualConfig.width, actualConfig.height, QImage::Format_RGB32);
    frame.setDotsPerMeterX(std::lrint((actualConfig.canvasDpi * 1000) / engraving::INCH));
    frame.setDotsPerMeterY(std::lrint((actualConfig.canvasDpi * 1000) / engraving::INCH));

    QPainter qp(&frame);
    qp.setRenderHint(QPainter::Antialiasing, true);
    qp.setRenderHint(QPainter::TextAntialiasing, true);

    Painter painter(&qp, "video_writer");

    // Setup duration
    INotationPlaybackPtr playback = notation->masterNotation()->playback();
    float totalPlayTimeSec = playback->totalPlayTime();

    int leadingFrameCount = static_cast<int>(actualConfig.leadingSec * actualConfig.fps);
    int scoreFrameCount = static_cast<int>(totalPlayTimeSec * actualConfig.fps);
    int trailingFrameCount = static_cast<int>(actualConfig.trailingSec * actualConfig.fps);
    int totalFrameCount = leadingFrameCount + scoreFrameCount + trailingFrameCount;
    LOGI() << "totalPlayTime: " << totalPlayTimeSec << " sec" << " frame count " << totalFrameCount;

    m_progress.start();

    // Add score title
    if (!generateLeadingFrames(encoder, notation, painter, frame, actualConfig, totalFrameCount)) {
        return;
    }

    // Add score frames
    if (!generateScoreFrames(encoder, notation, painter, frame, actualConfig, totalPlayTimeSec, leadingFrameCount, totalFrameCount)) {
        return;
    }

    // Add "Made with MuseScore"
    if (!generateTrailingFrames(encoder, actualConfig)) {
        return;
    }

    m_writeRet = muse::make_ok();
    m_progress.finish(muse::make_ok());
}

bool VideoWriter::generateLeadingFrames(muse::media::IVideoEncoderPtr encoder, INotationPtr notation,
                                        Painter& painter, QImage& frame,
                                        const Config& config, int totalFrameCount)
{
    int leadingFrameCount = static_cast<int>(config.leadingSec * config.fps);
    if (leadingFrameCount <= 0) {
        return true;
    }

    muse::String title = notationTitle(notation);
    muse::String subtitle = notationSubtitle(notation);

    auto scaledFontPointSize = [&config](double basePixelSize) {
        double pixelSize = basePixelSize * config.height / 1080.0;
        return pixelSize * 72.0 / engraving::DPI;
    };

    Font titleFont(Font::FontFamily(u"Muse Sans"), Font::Type::Text);
    titleFont.setPointSizeF(scaledFontPointSize(128.0));
    titleFont.setWeight(Font::Weight::Medium);

    Font subtitleFont(titleFont);
    subtitleFont.setPointSizeF(scaledFontPointSize(48.0));

    muse::RectF frameRect = muse::RectF::fromQRectF(QRectF(frame.rect()));

    const double maxTextWidth = frameRect.width() * 0.9;
    const double textLeft = (frameRect.width() - maxTextWidth) / 2.0;

    auto lineCount = [&](const FontMetrics& fm, const muse::String& text) {
        if (text.isEmpty()) {
            return 0;
        }
        double textWidth = fm.horizontalAdvance(text);
        return std::max(1, static_cast<int>(std::ceil(textWidth / maxTextWidth)));
    };

    FontMetrics titleFontMetrics(titleFont);
    const int titleLines = lineCount(titleFontMetrics, title);
    const double titleHeight = titleLines * titleFontMetrics.lineSpacing();

    FontMetrics subtitleFontMetrics(subtitleFont);
    const int subtitleLines = lineCount(subtitleFontMetrics, subtitle);
    const double subtitleHeight = subtitleLines * subtitleFontMetrics.lineSpacing();

    double centerY = frameRect.center().y();
    double titleTop = centerY - titleHeight / 2.0;
    muse::RectF titleRect(textLeft, titleTop, maxTextWidth, titleHeight);

    const double subtitleOffset = config.height / 20.0;
    double subtitleTop = titleRect.bottom() + subtitleOffset;
    muse::RectF subtitleRect(textLeft, subtitleTop, maxTextWidth, subtitleHeight);

    for (int f = 0; f < leadingFrameCount; f++) {
        if (m_abort) {
            m_writeRet = make_ret(muse::Ret::Code::Cancel);
            m_progress.finish(m_writeRet);
            return false;
        }

        m_progress.progress(f, totalFrameCount);

        painter.fillRect(frameRect, Color::BLACK);
        painter.setPen(Color::WHITE);
        painter.setFont(titleFont);
        painter.drawText(titleRect, AlignCenter, TextWordWrap, title);

        if (!subtitle.isEmpty()) {
            painter.setFont(subtitleFont);
            painter.drawText(subtitleRect, AlignCenter, TextWordWrap, subtitle);
        }

        encoder->encodeImage(frame);
    }

    return true;
}

bool VideoWriter::generateTrailingFrames(muse::media::IVideoEncoderPtr encoder, const Config& config)
{
    int trailingFrameCount = static_cast<int>(config.trailingSec * config.fps);
    if (trailingFrameCount <= 0) {
        return true;
    }

    static const muse::io::path_t RESOURCE_PATH = ":/videoexport/internal/resources/video_made_with.mp4";
    muse::ByteArray videoData = fileSystem()->readFile(RESOURCE_PATH).val;

    encoder->encodeVideo(videoData, trailingFrameCount);

    return true;
}

bool VideoWriter::generateScoreFrames(muse::media::IVideoEncoderPtr encoder, INotationPtr notation,
                                      Painter& painter, QImage& frame,
                                      const Config& config, float totalPlayTimeSec,
                                      int leadingFrameCount, int totalFrameCount)
{
    int scoreFrameCount = static_cast<int>(totalPlayTimeSec * config.fps);
    if (scoreFrameCount <= 0) {
        return true;
    }

    PageList pages = notation->elements()->pages();
    auto painting = notation->painting();
    INotationPlaybackPtr playback = notation->masterNotation()->playback();
    muse::RectF frameRect = muse::RectF::fromQRectF(QRectF(frame.rect()));

    auto pageByTick = [](const PageList& pages, tick_t tick) -> const Page* {
        for (const Page* p : pages) {
            if (tick < static_cast<tick_t>(p->endTick().ticks())) {
                return p;
            }
        }
        return nullptr;
    };

    const Color CURSOR_COLOR = Color(2, 109, 203, 127);

    PlaybackCursor cursor(iocContext());
    cursor.setNotation(notation);

    for (int f = 0; f < scoreFrameCount; f++) {
        if (m_abort) {
            m_writeRet = make_ret(muse::Ret::Code::Cancel);
            m_progress.finish(m_writeRet);
            return false;
        }

        m_progress.progress(leadingFrameCount + f, totalFrameCount);

        float currentTimeSec = static_cast<float>(f) / config.fps;
        if (currentTimeSec > totalPlayTimeSec) {
            currentTimeSec = totalPlayTimeSec;
        }

        tick_t tick = playback->secToTick(currentTimeSec);

        const Page* page = pageByTick(pages, tick);
        if (!page) {
            break;
        }

        if (!page->firstMeasure()) {
            // Skip pages with no notation
            continue;
        }

        INotationPainting::Options opt;
        opt.fromPage = static_cast<int>(page->pageNumber());
        opt.toPage = opt.fromPage;
        opt.deviceDpi = config.canvasDpi;

        painter.fillRect(frameRect, Color::BLACK);

        painter.save();
        painter.translate(config.moveToCenter);

        painting->paintPrint(&painter, opt);

        cursor.move(tick);

        muse::RectF cursorRect = cursor.rect();
        muse::PointF pagePos = page->pos();
        muse::RectF cursorAbsRect = cursorRect.translated(-pagePos);

        painter.fillRect(cursorAbsRect, CURSOR_COLOR);

        painter.restore();

        encoder->encodeImage(frame);
    }

    return true;
}

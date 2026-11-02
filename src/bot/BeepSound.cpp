#include "bot/BeepSound.h"

#include <QByteArray>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#else
#include <QApplication>
#endif

namespace
{
void appendLE(QByteArray &out, quint32 value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.append(char((value >> (8 * i)) & 0xff));
}

// 16-bit mono PCM WAV of the reference bot's beep.
QByteArray makeWav()
{
    constexpr int kRate = 44100;
    constexpr double kDuration = 5.5;  // total duration (s)
    constexpr double kAttack = 0.001;  // linear ramp to full volume (s)
    constexpr double kTau = 0.30;      // decay time constant (s)
    constexpr double kTone = 2642.0;   // primary tone (E7)
    constexpr double kOvertone = 6869.0;
    constexpr double kOvertoneGain = 0.08;
    constexpr double kPi = 3.14159265358979323846;

    const int samples = int(kRate * kDuration);
    QByteArray pcm;
    pcm.reserve(samples * 2);
    for (int i = 0; i < samples; ++i) {
        const double t = double(i) / kRate;
        const double env = t < kAttack ? t / kAttack : std::exp(-(t - kAttack) / kTau);
        const double s = (std::sin(2.0 * kPi * kTone * t)
                          + kOvertoneGain * std::sin(2.0 * kPi * kOvertone * t)) * env;
        // The two partials peak at 1.08: scale into range instead of clipping.
        const double v = std::clamp(s / (1.0 + kOvertoneGain), -1.0, 1.0);
        appendLE(pcm, quint32(quint16(qint16(std::lround(v * 32767.0)))), 2);
    }

    QByteArray wav;
    wav.reserve(44 + pcm.size());
    wav.append("RIFF", 4);
    appendLE(wav, quint32(36 + pcm.size()), 4);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    appendLE(wav, 16, 4);          // fmt chunk size
    appendLE(wav, 1, 2);           // PCM
    appendLE(wav, 1, 2);           // mono
    appendLE(wav, kRate, 4);
    appendLE(wav, kRate * 2, 4);   // byte rate
    appendLE(wav, 2, 2);           // block align
    appendLE(wav, 16, 2);          // bits per sample
    wav.append("data", 4);
    appendLE(wav, quint32(pcm.size()), 4);
    wav.append(pcm);
    return wav;
}
} // namespace

namespace BeepSound
{
void play()
{
#ifdef Q_OS_WIN
    // Built once and kept: PlaySound(SND_ASYNC) reads the buffer while it plays.
    static const QByteArray wav = makeWav();
    PlaySoundW(reinterpret_cast<LPCWSTR>(wav.constData()), nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    QApplication::beep();
#endif
}
} // namespace BeepSound

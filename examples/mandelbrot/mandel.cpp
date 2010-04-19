/*
    Copyright (C) 2010 Matthias Kretz <kretz@kde.org>

    Permission to use, copy, modify, and distribute this software
    and its documentation for any purpose and without fee is hereby
    granted, provided that the above copyright notice appear in all
    copies and that both that the copyright notice and this
    permission notice and warranty disclaimer appear in supporting
    documentation, and that the name of the author not be used in
    advertising or publicity pertaining to distribution of the
    software without specific, written prior permission.

    The author disclaim all warranties with regard to this
    software, including all implied warranties of merchantability
    and fitness.  In no event shall the author be liable for any
    special, indirect or consequential damages or any damages
    whatsoever resulting from loss of use, data or profits, whether
    in an action of contract, negligence or other tortious action,
    arising out of or in connection with the use or performance of
    this software.

*/

#include "mandel.h"
#include <QMutexLocker>
#include <QtCore/QtDebug>
#include "tsc.h"

using Vc::float_v;
using Vc::int_v;
using Vc::int_m;

template<MandelImpl Impl>
Mandel<Impl>::Mandel(QObject *_parent)
    : MandelBase(_parent)
{
}

MandelBase::MandelBase(QObject *_parent)
    : QThread(_parent),
    m_restart(false), m_abort(false)
{
}

MandelBase::~MandelBase()
{
    m_mutex.lock();
    m_abort = true;
    m_wait.wakeOne();
    m_mutex.unlock();

    wait();
}

void MandelBase::brot(const QSize &size, float x, float y, float scale)
{
    QMutexLocker lock(&m_mutex);

    m_size = size;
    m_x = x;
    m_y = y;
    m_scale = scale;

    if (!isRunning()) {
        start(LowPriority);
    } else {
        m_restart = true;
        m_wait.wakeOne();
    }
}

void MandelBase::run()
{
    while (!m_abort) {
        // first we copy the parameters to our local data so that the main main thread can give a
        // new task while we're working
        m_mutex.lock();
        // destination image, RGB is good - no need for alpha
        QImage image(m_size, QImage::Format_RGB32);
        float x = m_x;
        float y = m_y;
        float scale = m_scale;
        m_mutex.unlock();

        // benchmark the number of cycles it takes
        TimeStampCounter timer;
        timer.Start();

        // calculate the mandelbrot set/image
        mandelMe(image, x, y, scale);

        // if no new set was requested in the meantime - return the finished image
        if (!m_restart) {
            timer.Stop();
            emit ready(image, timer.Cycles());
        }

        // wait for more work
        m_mutex.lock();
        if (!m_restart) {
            m_wait.wait(&m_mutex);
        }
        m_restart = false;
        m_mutex.unlock();
    }
}

static const float S = 4.f;
static const int maxIterations = 255;

static int min(int a, int b) { return a < b ? a : b; }

template<>
void Mandel<VcImpl>::mandelMe(QImage &image, float x, float y, float scale)
{
    for (int yy = 0; yy < image.height(); ++yy) {
        uchar *line = image.scanLine(yy);
        const float_v c_imag = y + yy * scale;
        for (int_v xx(Vc::IndexesFromZero); !(xx < image.width()).isEmpty(); xx += float_v::Size) {
            Z c(x + static_cast<float_v>(xx) * scale, c_imag);
            Z z = c;
            int_v n(Vc::Zero);
            int_m inside = std::norm(z) < S;
            while (!(inside && n < maxIterations).isEmpty()) {
                z = P(z, c);
                inside = std::norm(z) < S;
                ++n(inside);
            }
            int_v colorValue = 255 - n;
            const int maxJ = min(int_v::Size, image.width() - xx[0]);
            for (int j = 0; j < maxJ; ++j) {
                const uchar value = colorValue[j];
                line[0] = value;
                line[1] = value;
                line[2] = value;
                line += 4;
            }
        }
        if (restart()) {
            break;
        }
    }
}

template<>
void Mandel<ScalarImpl>::mandelMe(QImage &image, float x, float y, float scale)
{
    //image.fill(0);
    for (int yy = 0; yy < image.height(); ++yy) {
        uchar *line = image.scanLine(yy);
        const float c_imag = y + yy * scale;
        for (int xx = 0; xx < image.width(); ++xx) {
            Z c(x + xx * scale, c_imag);
            Z z = c;
            int n = 0;
            for (n = 0; n < maxIterations && std::norm(z) < S; ++n) {
                z = P(z, c);
            }
            const uchar colorValue = 255 - n;
            line[0] = colorValue;
            line[1] = colorValue;
            line[2] = colorValue;
            line += 4;
        }
        if (restart()) {
            break;
        }
    }
}

template class Mandel<VcImpl>;
template class Mandel<ScalarImpl>;

// vim: sw=4 sts=4 et tw=100
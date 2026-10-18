// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

// Two threads. The camera thread takes the outer camera's frames as they come, 30 a second, and keeps the newest for
// latestFrame(); the decode thread looks for a QR code in the middle of one of them with ZXing-cpp, up to ten times a
// second.

#include "services/qr.hpp"

#include "services/qr_zxing.hpp"
#include "sys/log.hpp"
#include "sys/thread.hpp"

#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>

namespace tz
{

namespace
{

constexpr size_t kPixels = static_cast<size_t>(QrScanner::kWidth) * QrScanner::kHeight;
constexpr u32 kFrameBytes = kPixels * sizeof(uint16_t);

// The camera thread goes on the system core when the app may use it, and otherwise shares the app core with the main
// thread. Either way it runs just above the main thread: once a frame is in, the next transfer has to be set up before
// the camera port's small buffer overflows. Besides that it only copies the frame, a millisecond or so.
constexpr int kSystemCore = 1;
constexpr int kAppCore = -2;
constexpr int kCapturePriority = priority::kMain - 1;
constexpr size_t kCaptureStack = 32 * 1024;

// An attempt starts 100 ms after the last one started, or 10 ms after it ended when it took longer, as on an Old 3DS:
// the system doesn't take turns between threads of one priority, and the job workers on this core have this one's.
constexpr s64 kDecodeIntervalNs = 100'000'000;
constexpr s64 kDecodeGapNs = 10'000'000;
constexpr size_t kDecodeStack = 64 * 1024;

// ZXing-cpp reads the middle of the picture, around the corners the screen shows to aim with (240 x 240 of the
// camera's pixels): an Old 3DS took 0.17 s to convert a whole picture and look through it, about twice this.
constexpr int kDecodeWidth = 400;
constexpr int kDecodeHeight = 300;
constexpr size_t kDecodePixels = static_cast<size_t>(kDecodeWidth) * kDecodeHeight;

// How long stopping waits for the frame in flight, in 1 ms steps; a frame takes 33.
constexpr int kStopPolls = 500;

// The camera driver, started by the first scanner and kept until the app ends. An Old 3DS took 0.1 s to start it the
// first time, but 1.2 s when it had stopped it shortly before, and 0.6 s to stop it.
std::atomic<bool> driverStarted{false};

// Logs a camera call that failed; true if it worked.
bool check(Result result, const char* what)
{
    if (R_FAILED(result))
        LOG_ERROR("qr: couldn't %s (%08lX)", what, static_cast<unsigned long>(result));
    return R_SUCCEEDED(result);
}

// camInit() asks srv for cam:u, then starts the camera driver.
Error cameraError(Result result)
{
    // srv refuses services that aren't in the title's access list, and some ways of starting homebrew don't give it
    // cam:u
    if (R_MODULE(result) == RM_SRV || R_DESCRIPTION(result) == RD_NOT_AUTHORIZED)
        return Error{Errc::NotAllowed, static_cast<int32_t>(result), "no access to cam:u"};
    // the driver didn't start: something else is using the camera
    return Error{Errc::Busy, static_cast<int32_t>(result), "camera driver didn't start"};
}

// The middle of a frame (kDecodeWidth x kDecodeHeight) as 8-bit grey, which ZXing-cpp reads. (2R + 5G + B) / 8 of the
// channels widened to 8 bits is close to the usual luma weights (0.30, 0.59, 0.11), keeps white at 255 and needs no
// division.
void middleToGrey(const uint16_t* rgb565, uint8_t* grey)
{
    constexpr int left = (QrScanner::kWidth - kDecodeWidth) / 2;
    constexpr int top = (QrScanner::kHeight - kDecodeHeight) / 2;
    for (int y = 0; y < kDecodeHeight; ++y)
    {
        const uint16_t* row = rgb565 + static_cast<size_t>(top + y) * QrScanner::kWidth + left;
        for (int x = 0; x < kDecodeWidth; ++x)
        {
            const uint32_t pixel = row[x];
            const uint32_t r = pixel >> 11;
            const uint32_t g = (pixel >> 5) & 0x3F;
            const uint32_t b = pixel & 0x1F;
            const uint32_t r8 = (r << 3) | (r >> 2);
            const uint32_t g8 = (g << 2) | (g >> 4);
            const uint32_t b8 = (b << 3) | (b >> 2);
            *grey++ = static_cast<uint8_t>((2 * r8 + 5 * g8 + b8) >> 3);
        }
    }
}

// NULs and white space, which some generators leave after the text.
bool isPadding(char c)
{
    return c == '\0' || c == ' ' || (c >= '\t' && c <= '\r');
}

} // namespace

struct QrScanner::Impl
{
    ~Impl();

    Expected<void> open();

    // The camera thread: starts the driver, captures until the stop signal, stops the driver.
    void capture();
    // False if capturing ended without the stop signal: it didn't start, or the camera stopped.
    bool captureFrames();
    bool configureCamera(Handle& bufferError, s16& transferUnit);
    bool receiveInto(Handle& received, uint16_t* buffer, s16 transferUnit);
    void publish(const uint16_t* frame);

    // The decode thread.
    void decode();
    // Waits up to timeoutNs for the stop signal; true once it is given.
    bool stopping(s64 timeoutNs) const;

    // Sticky, so that both threads see it.
    Handle stopEvent = 0;

    // The camera writes frames here by DMA, taking turns, so one is read while the next comes in.
    uint16_t* dma[2] = {};

    // The newest frame for latestFrame(), how many there have been and how many it has seen. The main thread only
    // tries the lock, so it never waits for the camera thread.
    Mutex frameMutex;
    std::unique_ptr<uint16_t[]> latest;
    uint32_t frames = 0;
    uint32_t framesShown = 0;

    // The decode thread's frame: the camera thread copies the next one in when it is wanted, and leaves it alone until
    // it is wanted again. No lock, as the decode thread runs below the main thread and mustn't hold one the main
    // thread could wait for.
    std::unique_ptr<uint16_t[]> decodeFrame;
    std::atomic<bool> decodeWants{true};

    // The decode thread's own: the middle of its frame in grey.
    std::unique_ptr<uint8_t[]> grey;

    // Written once, before found is set.
    std::string resultText;
    std::atomic<bool> found{false};

    // Written once by the camera thread, before failed is set.
    Error failure;
    std::atomic<bool> failed{false};

    Thread captureThread;
    Thread decodeThread;
};

QrScanner::Impl::~Impl()
{
    // Both threads watch the stop event, and the camera thread stops the driver on its way out, so that nothing can
    // still be writing to the buffers once it has ended.
    if (stopEvent)
        svcSignalEvent(stopEvent);
    decodeThread.join();
    captureThread.join();

    for (uint16_t* buffer : dma)
    {
        if (buffer)
            linearFree(buffer);
    }
    if (stopEvent)
        svcCloseHandle(stopEvent);
}

// On failure the destructor undoes whatever was done.
Expected<void> QrScanner::Impl::open()
{
    const Result created = svcCreateEvent(&stopEvent, RESET_STICKY);
    if (R_FAILED(created))
    {
        stopEvent = 0;
        return fail(Errc::OutOfMemory, "stop event", created);
    }

    for (uint16_t*& buffer : dma)
        buffer = static_cast<uint16_t*>(linearAlloc(kFrameBytes));
    latest.reset(new (std::nothrow) uint16_t[kPixels]);
    decodeFrame.reset(new (std::nothrow) uint16_t[kPixels]);
    grey.reset(new (std::nothrow) uint8_t[kDecodePixels]);
    if (!dma[0] || !dma[1] || !latest || !decodeFrame || !grey)
        return fail(Errc::OutOfMemory, "QR scanner buffers");

    // The decode thread first: if the camera thread can't start, it alone is waited for, and it waits for nothing but
    // the stop signal.
    decodeThread = Thread::start([this] { decode(); }, priority::kWorker, kAppCore, kDecodeStack);
    if (!decodeThread.running())
        return fail(Errc::OutOfMemory, "QR decode thread");
    captureThread = Thread::start([this] { capture(); }, kCapturePriority, kSystemCore, kCaptureStack);
    if (!captureThread.running())
        return fail(Errc::OutOfMemory, "camera thread");
    return {};
}

void QrScanner::Impl::capture()
{
    const Result opened = driverStarted.load(std::memory_order_acquire) ? 0 : camInit();
    if (R_FAILED(opened))
    {
        failure = cameraError(opened);
        failed.store(true, std::memory_order_release);
        return;
    }
    driverStarted.store(true, std::memory_order_release);
    // stopped while the driver was starting: no need to turn the camera on
    if (!stopping(0) && !captureFrames())
    {
        failure = Error{Errc::Busy, 0, "camera stopped capturing"};
        failed.store(true, std::memory_order_release);
    }
}

bool QrScanner::Impl::captureFrames()
{
    // What the thread waits for: the stop signal, the end of a frame's transfer, the camera port's buffer overflowing.
    Handle events[3] = {stopEvent, 0, 0};
    Handle& received = events[1];
    Handle& bufferError = events[2];
    s16 transferUnit = 0;
    int filling = 0; // the DMA buffer the camera writes to

    bool capturing = configureCamera(bufferError, transferUnit) && receiveInto(received, dma[filling], transferUnit) &&
                     check(CAMU_StartCapture(PORT_CAM1), "start capturing");
    while (capturing)
    {
        s32 index = -1;
        if (!check(svcWaitSynchronizationN(&index, events, 3, false, U64_MAX), "wait for the camera") || index == 0)
            break;

        // each transfer has an event of its own
        svcCloseHandle(received);
        received = 0;
        if (index == 1)
        {
            // the camera carries on into the other buffer while this frame is handed over
            const uint16_t* frame = dma[filling];
            filling ^= 1;
            capturing = receiveInto(received, dma[filling], transferUnit);
            // The CPU's caches may still hold lines of this buffer from before the transfer; dropping them makes sure
            // the frame is read as the camera wrote it. The buffers are whole cache lines, so nothing else is lost.
            GSPGPU_InvalidateDataCache(frame, kFrameBytes);
            publish(frame);
        }
        else
        {
            // The next transfer wasn't set up in time, the port's buffer overflowed and capturing stopped: start
            // again. Clearing the event matters only if the camera module made it sticky.
            svcClearEvent(bufferError);
            CAMU_ClearBuffer(PORT_CAM1);
            capturing = receiveInto(received, dma[filling], transferUnit) &&
                        check(CAMU_StartCapture(PORT_CAM1), "restart capturing");
        }
    }

    // Off again, after the frame in flight, so that nothing is being written when the buffers are freed.
    CAMU_StopCapture(PORT_CAM1);
    bool busy = true;
    for (int i = 0; i < kStopPolls && R_SUCCEEDED(CAMU_IsBusy(&busy, PORT_CAM1)) && busy; ++i)
        sleepMs(1);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    if (received)
        svcCloseHandle(received);
    if (bufferError)
        svcCloseHandle(bufferError);
    return stopping(0);
}

bool QrScanner::Impl::configureCamera(Handle& bufferError, s16& transferUnit)
{
    // The whole picture, in the GPU's RGB565. The outer camera's pictures come upright and unmirrored, rows from the
    // top, as latestFrame() promises.
    if (!check(CAMU_SetSize(SELECT_OUT1, SIZE_VGA, CONTEXT_A), "set the picture size") ||
        !check(CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A), "set the picture format"))
        return false;

    // Picture quality: worth asking for, not worth giving up over. Letter mode as PKSM and Universal-Updater set it:
    // what it changes isn't documented, and the settings after it may replace part of it.
    check(CAMU_SetPhotoMode(SELECT_OUT1, PHOTO_MODE_LETTER), "set the photo mode");
    check(CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30), "set the frame rate");
    check(CAMU_SetNoiseFilter(SELECT_OUT1, true), "turn on the noise filter");
    check(CAMU_SetAutoExposure(SELECT_OUT1, true), "turn on auto exposure");
    check(CAMU_SetAutoWhiteBalance(SELECT_OUT1, true), "turn on auto white balance");
    // a little sharpening: crisper module edges make a code's corners easier to find
    check(CAMU_SetSharpness(SELECT_OUT1, 1), "set the sharpness");

    if (!check(CAMU_Activate(SELECT_OUT1), "turn on the camera"))
        return false;
    if (!check(CAMU_GetBufferErrorInterruptEvent(&bufferError, PORT_CAM1), "get the buffer error event"))
    {
        bufferError = 0; // not a handle when the call failed
        return false;
    }

    // Whole pictures, moved in the biggest pieces the port allows.
    u32 maxBytes = 0;
    if (!check(CAMU_SetTrimming(PORT_CAM1, false), "turn off trimming") ||
        !check(CAMU_GetMaxBytes(&maxBytes, kWidth, kHeight), "get the transfer size"))
        return false;
    // CAMU_SetReceiving takes it as an s16
    if (maxBytes == 0 || maxBytes > static_cast<u32>(INT16_MAX))
    {
        LOG_ERROR("qr: unexpected camera transfer size %lu", static_cast<unsigned long>(maxBytes));
        return false;
    }
    transferUnit = static_cast<s16>(maxBytes);
    return check(CAMU_SetTransferBytes(PORT_CAM1, maxBytes, kWidth, kHeight), "set the transfer size") &&
           check(CAMU_ClearBuffer(PORT_CAM1), "clear the camera buffer");
}

// Sets up the transfer of the next frame into buffer.
bool QrScanner::Impl::receiveInto(Handle& received, uint16_t* buffer, s16 transferUnit)
{
    if (check(CAMU_SetReceiving(&received, buffer, PORT_CAM1, kFrameBytes, transferUnit), "receive a frame"))
        return true;
    received = 0; // not a handle when the call failed
    return false;
}

void QrScanner::Impl::publish(const uint16_t* frame)
{
    {
        std::lock_guard lock(frameMutex);
        std::memcpy(latest.get(), frame, kFrameBytes);
        ++frames;
    }
    if (decodeWants.load(std::memory_order_acquire))
    {
        std::memcpy(decodeFrame.get(), frame, kFrameBytes);
        decodeWants.store(false, std::memory_order_release);
    }
}

void QrScanner::Impl::decode()
{
    s64 waitNs = kDecodeIntervalNs;
    while (!stopping(waitNs))
    {
        waitNs = kDecodeIntervalNs;
        if (decodeWants.load(std::memory_order_acquire))
            continue; // no frame since the last one
        const u64 started = svcGetSystemTick();
        middleToGrey(decodeFrame.get(), grey.get());
        // the grey copy is enough, so the next frame can come in while ZXing-cpp looks
        decodeWants.store(true, std::memory_order_release);

        std::optional<std::string> text = readQrCode(grey.get(), kDecodeWidth, kDecodeHeight);
        while (text && !text->empty() && isPadding(text->back()))
            text->pop_back();
        if (text && !text->empty())
        {
            LOG_INFO("qr: read a code of %zu bytes", text->size());
            resultText = std::move(*text);
            found.store(true, std::memory_order_release);
            // The first code is the answer. The camera stays on for the picture until the scanner goes.
            return;
        }
        const s64 tookNs = static_cast<s64>((svcGetSystemTick() - started) * 1'000'000'000ULL / SYSCLOCK_ARM11);
        waitNs = std::max(kDecodeGapNs, kDecodeIntervalNs - tookNs);
    }
}

bool QrScanner::Impl::stopping(s64 timeoutNs) const
{
    // Anything but a timeout ends the wait for good, a failed wait included, so a bad handle can't leave the thread
    // spinning.
    return R_DESCRIPTION(svcWaitSynchronization(stopEvent, timeoutNs)) != RD_TIMEOUT;
}

Expected<std::unique_ptr<QrScanner>> QrScanner::start()
{
    auto impl = std::make_unique<Impl>();
    TZ_CHECK(impl->open());
    return std::unique_ptr<QrScanner>(new QrScanner(std::move(impl)));
}

QrScanner::QrScanner(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

// Impl's destructor stops the threads and the camera.
QrScanner::~QrScanner() = default;

bool QrScanner::latestFrame(std::vector<uint16_t>& frame)
{
    // Sized first, so that the camera thread never waits for an allocation. While the camera thread is copying a
    // frame in, this one reports nothing new rather than wait; the next call gets it.
    frame.resize(kPixels);
    std::unique_lock lock(impl_->frameMutex, std::try_to_lock);
    if (!lock.owns_lock() || impl_->frames == impl_->framesShown)
        return false;
    std::memcpy(frame.data(), impl_->latest.get(), kFrameBytes);
    impl_->framesShown = impl_->frames;
    return true;
}

void QrScanner::stopDriver()
{
    if (driverStarted.exchange(false))
        camExit();
}

std::optional<Error> QrScanner::failure() const
{
    if (!impl_->failed.load(std::memory_order_acquire))
        return std::nullopt;
    return impl_->failure;
}

void QrScanner::stop()
{
    svcSignalEvent(impl_->stopEvent);
}

bool QrScanner::stopped() const
{
    return impl_->decodeThread.done() && impl_->captureThread.done();
}

std::optional<std::string> QrScanner::result() const
{
    if (!impl_->found.load(std::memory_order_acquire))
        return std::nullopt;
    return impl_->resultText;
}

} // namespace tz

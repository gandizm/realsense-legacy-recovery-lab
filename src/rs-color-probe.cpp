// Read-only legacy color-stream probe.
//   enumerate -> list native color modes
//   640       -> color 640x480 YUYV @30
//   1080      -> color 1920x1080 YUYV @30
//   1080_15   -> color 1920x1080 YUYV @15
//   720       -> color 1280x720 YUYV @15
//   rgb8      -> color 640x480 RGB8 @30 (host conversion check)
#include <librealsense/rs.hpp>
#include "device.h"
#include "ds-private.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <thread>

static void print_firmware_state(rs::device * public_device, const char * tag)
{
    // rs_device_base exposes this handle only for the local diagnostic tools;
    // every operation below is an XU read (no stream or Flash write).
    auto * base = reinterpret_cast<rs_device_base *>(public_device);
    auto & uvc_device = base->get_uvc_device_for_flash_backup();
    std::printf("[%s]", tag);
    try
    {
        uint8_t status[4] = { 0, 0, 0, 0 };
        rsimpl::ds::xu_read(uvc_device, rsimpl::ds::lr_xu,
            rsimpl::ds::control::status, status, sizeof(status));
        const uint32_t packed = static_cast<uint32_t>(status[0]) |
            (static_cast<uint32_t>(status[1]) << 8) |
            (static_cast<uint32_t>(status[2]) << 16) |
            (static_cast<uint32_t>(status[3]) << 24);
        std::printf(" status=0x%08X", packed);
    }
    catch (const std::exception & e) { std::printf(" status=<error:%s>", e.what()); }
    try
    {
        const auto last_error = rsimpl::ds::get_last_error(uvc_device);
        std::printf(" last_error=0x%02X", static_cast<unsigned>(last_error));
    }
    catch (const std::exception & e) { std::printf(" last_error=<error:%s>", e.what()); }
    try
    {
        const auto exposure_mode = rsimpl::ds::get_lr_exposure_mode(uvc_device);
        std::printf(" lr_ae=%u", static_cast<unsigned>(exposure_mode));
    }
    catch (const std::exception & e) { std::printf(" lr_ae=<error:%s>", e.what()); }
    std::printf("\n");
}

static const char * mode_name(const char * arg)
{
    return arg ? arg : "640";
}

int main(int argc, char ** argv) try
{
    // Keep the probe read-only, but expose legacy SDK/XU startup diagnostics
    // when a color endpoint starts without delivering frames.
    rs::log_to_console(rs::log_severity::debug);
    rs::context ctx;
    if (ctx.get_device_count() == 0)
    {
        std::printf("No device detected.\n");
        return 2;
    }

    rs::device * dev = ctx.get_device(0);
    std::printf("Using %s, serial %s, firmware %s\n",
        dev->get_name(), dev->get_serial(), dev->get_firmware_version());

    const char * arg = argc > 1 ? argv[1] : "640";
    if (std::strcmp(arg, "intent-cycle") == 0)
    {
        // Volatile XU-only diagnostic: mirror the SDK's normal RGB intent
        // transition without opening a UVC stream.  No Flash is touched.
        auto * base = reinterpret_cast<rs_device_base *>(dev);
        auto & uvc_device = base->get_uvc_device_for_flash_backup();
        uint8_t intent = 0;
        print_firmware_state(dev, "intent_initial");
        rsimpl::ds::set_stream_intent(uvc_device, intent);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        print_firmware_state(dev, "intent_0");
        intent = static_cast<uint8_t>(rsimpl::ds::STATUS_BIT_WEB_STREAMING);
        rsimpl::ds::set_stream_intent(uvc_device, intent);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        print_firmware_state(dev, "intent_4_after_3s");
        intent = 0;
        rsimpl::ds::set_stream_intent(uvc_device, intent);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        print_firmware_state(dev, "intent_restored_0");
        return 0;
    }
    if (std::strcmp(arg, "enumerate") == 0)
    {
        int n = dev->get_stream_mode_count(rs::stream::color);
        std::printf("color mode count: %d\n", n);
        for (int m = 0; m < n; ++m)
        {
            int w, h, fps; rs::format f;
            dev->get_stream_mode(rs::stream::color, m, w, h, f, fps);
            std::printf("color mode %d: %dx%d %s @%d\n", m, w, h,
                rs_format_to_string((rs_format)f), fps);
        }
        return 0;
    }

    int width = 640, height = 480, fps = 30;
    rs::format format = rs::format::yuyv;
    if (std::strcmp(arg, "1080") == 0)
    {
        width = 1920; height = 1080;
    }
    else if (std::strcmp(arg, "1080_15") == 0)
    {
        width = 1920; height = 1080; fps = 15;
    }
    else if (std::strcmp(arg, "720") == 0)
    {
        width = 1280; height = 720; fps = 15;
    }
    else if (std::strcmp(arg, "rgb8") == 0)
    {
        format = rs::format::rgb8;
    }

    std::printf("enabling color %dx%d %s @%d\n", width, height,
        rs_format_to_string((rs_format)format), fps);
    print_firmware_state(dev, "before_start");
    dev->enable_stream(rs::stream::color, width, height, format, fps);
    dev->start();
    print_firmware_state(dev, "after_start");
    std::printf("Color stream started; waiting for 10 frames.\n");
    for (int i = 0; i < 10; ++i)
    {
        try
        {
            dev->wait_for_frames();
        }
        catch (const rs::error & e)
        {
            std::fprintf(stderr, "wait_for_frames failed at frame %02d: %s\n", i, e.what());
            break;
        }
        const uint8_t * data = reinterpret_cast<const uint8_t *>(dev->get_frame_data(rs::stream::color));
        if (!data)
        {
            std::printf("Frame %02d returned null data.\n", i);
            continue;
        }
        const size_t bytes_per_pixel = format == rs::format::rgb8 ? 3 : 2;
        const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * bytes_per_pixel;
        unsigned nonzero = 0;
        for (size_t p = 0; p < bytes; ++p) if (data[p]) ++nonzero;
        std::printf("Frame %02d nonzero_bytes=%u/%zu first=%02X %02X %02X %02X\n",
            i, nonzero, bytes, data[0], data[1], data[2], data[3]);
    }
    print_firmware_state(dev, "after_wait");
    std::printf("Color probe completed successfully.\n");
    dev->stop();
    return 0;
}
catch (const rs::error & e)
{
    std::fprintf(stderr, "RealSense error calling %s(%s):\n    %s\n",
        e.get_failed_function().c_str(), e.get_failed_args().c_str(), e.what());
    return 1;
}
catch (const std::exception & e)
{
    std::fprintf(stderr, "Exception: %s\n", e.what());
    return 1;
}

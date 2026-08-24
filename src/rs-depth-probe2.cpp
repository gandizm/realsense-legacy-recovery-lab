// rs-depth-probe2.cpp
// Depth start probe with selectable native depth format.
//   no arg      -> Z16 depth
//   emitter     -> Z16 depth with RS_OPTION_R200_EMITTER_ENABLED forced on
//   disparity   -> DISPARITY16 depth
//   ir          -> infrared 640x480 Y8 (subdevice 0 health check)
//   enumerate   -> list supported depth modes only
#include <librealsense/rs.hpp>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>

int main(int argc, char ** argv) try
{
    rs::log_to_console(rs::log_severity::warn);

    rs::context ctx;
    if (ctx.get_device_count() == 0)
    {
        std::printf("No device detected.\n");
        return 2;
    }

    rs::device * dev = ctx.get_device(0);
    std::printf("Using %s, serial %s, firmware %s\n",
        dev->get_name(), dev->get_serial(), dev->get_firmware_version());

    if (argc > 1 && std::strcmp(argv[1], "enumerate") == 0)
    {
        int n = dev->get_stream_mode_count(rs::stream::depth);
        std::printf("depth mode count: %d\n", n);
        for (int m = 0; m < n; ++m)
        {
            int w, h, fps; rs::format f;
            dev->get_stream_mode(rs::stream::depth, m, w, h, f, fps);
            std::printf("depth mode %d: %dx%d %s @%d\n", m, w, h,
                rs_format_to_string((rs_format)f), fps);
        }
        return 0;
    }

    bool use_disparity = argc > 1 && std::strcmp(argv[1], "disparity") == 0;
    bool force_emitter = argc > 1 && std::strcmp(argv[1], "emitter") == 0;
    bool use_ir = argc > 1 && std::strcmp(argv[1], "ir") == 0;

    if (use_ir)
    {
        std::printf("enabling infrared 640x480 Y8 @30\n");
        dev->enable_stream(rs::stream::infrared, 640, 480, rs::format::y8, 30);
        dev->start();
        std::printf("IR stream started; waiting for 10 frames.\n");
        for (int i = 0; i < 10; ++i)
        {
            dev->wait_for_frames();
            const uint8_t * data = reinterpret_cast<const uint8_t *>(dev->get_frame_data(rs::stream::infrared));
            if (!data) { std::printf("Frame %d null\n", i); continue; }
            unsigned nonzero = 0;
            for (int p = 0; p < 640 * 480; ++p) if (data[p]) ++nonzero;
            std::printf("IR frame %02d nonzero=%u\n", i, nonzero);
        }
        std::printf("IR probe completed.\n");
        dev->stop();
        return 0;
    }

    rs::format fmt = use_disparity ? rs::format::disparity16 : rs::format::z16;
    std::printf("enabling depth 640x480 %s @30\n", rs_format_to_string((rs_format)fmt));
    dev->enable_stream(rs::stream::depth, 640, 480, fmt, 30);
    if (force_emitter)
    {
        dev->set_option(rs::option::r200_emitter_enabled, 1.0);
        std::printf("emitter forced ON\n");
    }
    dev->start();

    std::printf("Depth stream started; waiting for 30 frames.\n");
    for (int i = 0; i < 30; ++i)
    {
        dev->wait_for_frames();
        const uint16_t * data = reinterpret_cast<const uint16_t *>(dev->get_frame_data(rs::stream::depth));
        if (!data)
        {
            std::printf("Frame %d returned null data.\n", i);
            continue;
        }
        unsigned valid = 0;
        uint16_t minv = std::numeric_limits<uint16_t>::max();
        uint16_t maxv = 0;
        for (int p = 0; p < 640 * 480; ++p)
        {
            uint16_t v = data[p];
            if (v)
            {
                ++valid;
                if (v < minv) minv = v;
                if (v > maxv) maxv = v;
            }
        }
        std::printf("Frame %02d valid=%u min=%u max=%u\n", i, valid, valid ? minv : 0, valid ? maxv : 0);
    }

    std::printf("Depth probe completed successfully.\n");
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

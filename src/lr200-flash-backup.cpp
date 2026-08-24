#include "device.h"
#include "ds-private.h"
#include <librealsense/rs.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    constexpr uint32_t flash_size = 0x100000;
    constexpr uint32_t page_size = 0x100;
    constexpr uint32_t pages_per_chunk = 16;
    constexpr uint32_t chunk_size = page_size * pages_per_chunk;

    void check(rs_error * error)
    {
        if (!error) return;
        throw std::runtime_error(rs_get_error_message(error));
    }
}

int main(int argc, char ** argv)
try
{
    if (argc != 2)
    {
        std::cerr << "usage: lr200-flash-backup <output.bin>\n";
        return 2;
    }

    rs_error * error = nullptr;
    rs_context * context = rs_create_context(RS_API_VERSION, &error);
    check(error);
    if (rs_get_device_count(context, &error) != 1)
    {
        check(error);
        throw std::runtime_error("exactly one legacy RealSense device must be connected");
    }

    rs_device * public_device = rs_get_device(context, 0, &error);
    check(error);
    auto * device = dynamic_cast<rs_device_base *>(public_device);
    if (!device) throw std::runtime_error("device is not an rs_device_base");

    std::cout << "device=" << rs_get_device_name(public_device, &error)
              << " firmware=" << rs_get_device_firmware_version(public_device, &error)
              << " bytes=" << flash_size << "\n";
    check(error);

    std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open output file");

    std::array<unsigned char, chunk_size> chunk = {};
    auto & uvc = device->get_uvc_device_for_flash_backup();
    for (uint32_t address = 0; address < flash_size; address += chunk_size)
    {
        if (!rsimpl::ds::read_device_pages(uvc, address, chunk.data(), pages_per_chunk))
            throw std::runtime_error("flash read rejected at address " + std::to_string(address));
        output.write(reinterpret_cast<const char *>(chunk.data()), chunk.size());
        if (!output) throw std::runtime_error("output write failed");
        if (((address + chunk_size) & 0xFFFF) == 0)
            std::cout << "read 0x" << std::hex << std::setw(6) << std::setfill('0')
                      << (address + chunk_size) << "/0x" << flash_size << std::dec << "\n";
    }

    output.close();
    rs_delete_context(context, &error);
    check(error);
    std::cout << "backup complete: " << argv[1] << "\n";
    return 0;
}
catch (const std::exception & e)
{
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
}

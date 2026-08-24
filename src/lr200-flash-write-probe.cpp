// lr200-flash-write-probe.cpp
// DS4 (R200/LR200) SPI flash write probe, derived from FWUpdateR200.exe
// disassembly (see FWUpdateR200_Flash_Opcode_Handoff.md).
//
// Opcodes confirmed from the updater binary:
//   0x1A download_spi_flash (read pages), 0x19 upload_spi_flash (write pages),
//   0x1B erase_sector, 0x1C pre/post handshake, 0x21 get_fwrevision.
//
// SAFETY: default mode is 'probe' which builds and prints every packet but
// sends NOTHING to the device. 'read' mode only issues 0x1A reads.
// 'write' mode additionally requires --commit AND the environment variable
// R200_WRITE_CONFIRM=I_UNDERSTAND, and is hard-limited to non-firmware NV.
// 'write-backup-bank' is a separate, exact-range recovery operation.  It only
// accepts [0x50000,0x80000), requires an expected-current full image, refuses
// if source/expected differ anywhere outside that window, and writes only the
// differing sectors with immediate read-back verification.

#include "device.h"
#include "ds-private.h"
#include <librealsense/rs.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t page_size = 0x100;
    constexpr uint32_t sector_size = 0x1000;
    constexpr uint32_t pages_per_sector = sector_size / page_size;
    constexpr uint32_t allowed_lo = 0xA0000;
    constexpr uint32_t allowed_hi = 0x100000; // whole non-firmware NV region
    constexpr uint32_t flash_size = 0x100000;

    // Command packet layout confirmed from FWUpdateR200.exe. The XU control
    // descriptor requires exactly 0x100 bytes on the wire (0xEC-byte payloads
    // are rejected with ERROR_INVALID_BUFFER_SIZE); fields occupy +0..+19.
    struct cmd_packet
    {
        uint32_t code = 0;
        uint32_t modifier = 0;
        // Intel's DS4 updater and librealsense legacy both use transaction
        // tag 12.  Tag 0 was tolerated for NV reads/writes but is not the
        // updater-equivalent command format for protected firmware sectors.
        uint32_t tag = 12;
        uint32_t address = 0;
        uint32_t value = 0;
        uint8_t reserved[0x100 - 20] = {};
    };
    static_assert(sizeof(cmd_packet) == 0x100, "cmd_packet must be 0x100 bytes");

    constexpr uint32_t mod_direct = 0x10;
    constexpr uint32_t mod_erase = 0x20;
    constexpr uint32_t op_read = 0x1A;
    constexpr uint32_t op_write = 0x19;
    constexpr uint32_t op_erase = 0x1B;
    constexpr uint32_t op_handshake = 0x1C;

    void hexdump(const std::string & label, const void * data, size_t n)
    {
        const auto * p = static_cast<const uint8_t *>(data);
        std::cout << label << ":";
        size_t lim = n < 24 ? n : 24;
        for (size_t i = 0; i < lim; ++i)
            std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0') << int(p[i]);
        std::cout << std::dec << (n > lim ? " ..." : "") << "\n";
    }

    void check(rs_error * error)
    {
        if (!error) return;
        throw std::runtime_error(rs_get_error_message(error));
    }

    // Mirrors the updater's send_command_and_receive_response: set_control a
    // complete 0x100-byte packet on lr_xu control 1, then get_control the reply.
    void send_cmd(rsimpl::uvc::device & uvc, const cmd_packet & pkt, bool really)
    {
        hexdump("  packet", &pkt, sizeof(pkt));
        if (!really)
        {
            std::cout << "  [probe] not sent\n";
            return;
        }
        cmd_packet copy = pkt;
        rsimpl::uvc::set_control(uvc, rsimpl::ds::lr_xu,
                                 static_cast<int>(rsimpl::ds::control::command_response),
                                 &copy, sizeof(copy));
        cmd_packet reply{};
        rsimpl::uvc::get_control(uvc, rsimpl::ds::lr_xu,
                                 static_cast<int>(rsimpl::ds::control::command_response),
                                 &reply, sizeof(reply));
        hexdump("  reply ", &reply, 32);
    }

    // Exact updater sequence for erase_sector: one tagged 0x1B command.
    void erase_sector(rsimpl::uvc::device & uvc, uint32_t address, bool really)
    {
        std::cout << "erase_sector 0x" << std::hex << address << std::dec << "\n";
        cmd_packet erase{}; erase.code = op_erase; erase.modifier = mod_erase; erase.address = address;
        send_cmd(uvc, erase, really);
    }

    // Sequence already proven on this target's writable NV sectors during the
    // donor-calibration repair: 0x1C pre, erase, 0x1C post, all with tag 0.
    void erase_sector_known_nv(rsimpl::uvc::device & uvc, uint32_t address)
    {
        cmd_packet hs{};
        hs.code = op_handshake; hs.modifier = mod_direct; hs.tag = 0;
        send_cmd(uvc, hs, true);
        cmd_packet erase{};
        erase.code = op_erase; erase.modifier = mod_erase;
        erase.tag = 0; erase.address = address;
        send_cmd(uvc, erase, true);
        send_cmd(uvc, hs, true);
    }

    // Updater sequence for write_pages: write packet then one set_control per page.
    void write_pages(rsimpl::uvc::device & uvc, uint32_t address, const uint8_t * data,
                     uint32_t n_pages, bool really)
    {
        std::cout << "write_pages 0x" << std::hex << address << " n=" << std::dec << n_pages << "\n";
        cmd_packet w{}; w.code = op_write; w.modifier = mod_direct;
        w.address = address; w.value = n_pages * page_size;
        send_cmd(uvc, w, really);
        for (uint32_t i = 0; i < n_pages; ++i)
        {
            const uint8_t * page = data + size_t(i) * page_size;
            if (!really)
            {
                hexdump("  page[probe]", page, 16);
                continue;
            }
            rsimpl::uvc::set_control(uvc, rsimpl::ds::lr_xu,
                                     static_cast<int>(rsimpl::ds::control::command_response),
                                     const_cast<uint8_t *>(page), page_size);
            std::cout << "  page " << i + 1 << "/" << n_pages << " written\n";
        }
    }

    void write_pages_known_nv(rsimpl::uvc::device & uvc, uint32_t address,
                              const uint8_t * data, uint32_t n_pages)
    {
        cmd_packet w{};
        w.code = op_write; w.modifier = mod_direct; w.tag = 0;
        w.address = address; w.value = n_pages * page_size;
        send_cmd(uvc, w, true);
        for (uint32_t i = 0; i < n_pages; ++i)
            rsimpl::uvc::set_control(uvc, rsimpl::ds::lr_xu,
                static_cast<int>(rsimpl::ds::control::command_response),
                const_cast<uint8_t *>(data + size_t(i) * page_size), page_size);
    }

    bool read_back(rsimpl::uvc::device & uvc, uint32_t address, uint8_t * out, uint32_t n_pages)
    {
        return rsimpl::ds::read_device_pages(uvc, address, out, n_pages);
    }

    std::vector<uint8_t> read_file(const std::string & path, size_t want)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open " + path);
        std::vector<uint8_t> buf(want);
        in.read(reinterpret_cast<char *>(buf.data()), want);
        if (size_t(in.gcount()) != want) throw std::runtime_error("short read on " + path);
        return buf;
    }
}

int main(int argc, char ** argv)
try
{
    if (argc < 2)
    {
        std::cerr << "usage:\n"
                  << "  lr200-flash-write-probe probe  <sector0> <n_sectors> <content.bin>\n"
                  << "  lr200-flash-write-probe read   <sector0> <n_sectors> [<ref.bin>]\n"
                  << "  lr200-flash-write-probe handshake                (size probe, benign 0x1C)\n"
                  << "  lr200-flash-write-probe write  <sector0> <n_sectors> <content.bin> --commit\n"
                  << "  lr200-flash-write-probe write-backup-bank <source-full.bin> <expected-current-full.bin> --commit\n"
                  << "  lr200-flash-write-probe write-backup-bank-pilot <source-full.bin> <expected-current-full.bin> --commit\n"
                  << "  lr200-flash-write-probe write-main-pilot <source-full.bin> <expected-current-full.bin> --commit\n"
                  << "  lr200-flash-write-probe write-a1-hybrid <source-full.bin> <expected-current-full.bin> --commit\n"
                  << "  lr200-flash-write-probe write-a3-candidate <source-full.bin> <expected-current-full.bin> --commit\n"
                  << "sectors are 4 KiB, address = sector * 0x1000; normal writable range [0xA0,0x100)\n";
        return 2;
    }

    std::string mode = argv[1];
    if (mode != "probe" && mode != "read" && mode != "write" && mode != "write-backup-bank" &&
        mode != "write-backup-bank-pilot" &&
        mode != "write-main-pilot" &&
        mode != "write-a1-hybrid" &&
        mode != "write-a3-candidate" &&
        mode != "handshake" && mode != "xu" && mode != "reset")
        throw std::runtime_error("unknown mode: " + mode);

    uint32_t sector0 = 0, n_sectors = 0, address = 0, total = 0;
    bool commit = false;
    const bool backup_bank_mode = mode == "write-backup-bank" || mode == "write-backup-bank-pilot";
    const bool backup_bank_pilot = mode == "write-backup-bank-pilot";
    const bool main_pilot_mode = mode == "write-main-pilot";
    const bool a1_hybrid_mode = mode == "write-a1-hybrid";
    const bool a3_candidate_mode = mode == "write-a3-candidate";
    if (backup_bank_mode || main_pilot_mode || a1_hybrid_mode || a3_candidate_mode)
    {
        if (argc != 5)
            throw std::runtime_error("special write mode requires source, expected-current, and --commit");
        sector0 = main_pilot_mode ? 0x10 : (a1_hybrid_mode ? 0xA1 : (a3_candidate_mode ? 0xA3 : 0x50));
        n_sectors = (main_pilot_mode || a1_hybrid_mode || a3_candidate_mode) ? 1 : 0x30;
        address = sector0 * sector_size;
        total = n_sectors * sector_size;
        commit = std::string(argv[4]) == "--commit";
    }
    else if (mode != "handshake" && mode != "xu" && mode != "reset")
    {
        sector0 = std::stoul(argv[2], nullptr, 0);
        n_sectors = std::stoul(argv[3], nullptr, 0);
        address = sector0 * sector_size;
        total = n_sectors * sector_size;
        commit = argc > 4 && std::string(argv[argc - 1]) == "--commit";
        if (n_sectors == 0 || address + total > flash_size)
            throw std::runtime_error("sector range out of flash bounds");
    }

    if (mode == "probe")
    {
        std::vector<uint8_t> content;
        if (argc >= 5)
        {
            auto full = read_file(argv[4], flash_size);
            content.assign(full.begin() + address, full.begin() + address + total);
        }
        else
            content.assign(total, 0xEE);
        std::cout << "[probe mode: packets shown, NOTHING is sent to the device]\n";
        // Build packets without a device: print packet layouts only.
        for (uint32_t s = 0; s < n_sectors; ++s)
        {
            uint32_t a = address + s * sector_size;
            std::cout << "-- sector 0x" << std::hex << a << std::dec << " --\n";
            cmd_packet erase{}; erase.code = op_erase; erase.modifier = mod_erase; erase.address = a;
            hexdump("  erase pkt  ", &erase, sizeof(erase));
            cmd_packet hs{}; hs.code = op_handshake; hs.modifier = mod_direct;
            hexdump("  pre hs pkt ", &hs, sizeof(hs));
            hexdump("  post hs pkt", &hs, sizeof(hs));
            cmd_packet w{}; w.code = op_write; w.modifier = mod_direct;
            w.address = a; w.value = pages_per_sector * page_size;
            hexdump("  write pkt  ", &w, sizeof(w));
            hexdump("  page 0     ", content.data() + size_t(s) * sector_size, 16);
        }
        std::cout << "[probe done; device untouched]\n";
        return 0;
    }

    // Modes that need the device.
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
              << " firmware=" << rs_get_device_firmware_version(public_device, &error) << "\n";
    check(error);
    auto & uvc = device->get_uvc_device_for_flash_backup();

    if (mode == "reset")
    {
        // Firmware soft reset (XU control 16), equivalent to a replug.
        // The write always "fails" because the firmware stops responding.
        std::cout << "sending sw_reset...\n";
        rsimpl::ds::force_firmware_reset(uvc);
        std::cout << "sw_reset sent; device should re-enumerate\n";
        rs_delete_context(context, &error);
        return 0;
    }

    if (mode == "xu")
    {
        // Probe XU control selectors: try GET/SET at 1 and 4 byte lengths.
        // intent/status/emitter values written are non-destructive.
        uint8_t emitter_initial = 0;
        try { rsimpl::uvc::get_control(uvc, rsimpl::ds::lr_xu, 8, &emitter_initial, 1); } catch (...) {}
        struct probe { int ctrl; const char * name; bool try_set; };
        const probe probes[] = {
            { 3, "stream_intent", true }, { 4, "depth_units", false },
            { 5, "min_max", false },      { 6, "disparity", true },
            { 8, "emitter", true },       { 9, "temperature", false },
            { 20, "status", false },
        };
        for (const auto & pr : probes)
        {
            // disparity uses a 12-byte struct; others are 1/4 byte scalars
            std::vector<int> sizes = (pr.ctrl == 6) ? std::vector<int>{ 4, 12 } : std::vector<int>{ 1, 4 };
            for (int len : sizes)
            {
                uint8_t buf[16] = {};
                std::cout << "GET ctrl " << pr.ctrl << " (" << pr.name << ") len " << len << ": ";
                try
                {
                    rsimpl::uvc::get_control(uvc, rsimpl::ds::lr_xu, pr.ctrl, buf, len);
                    std::cout << "ok [";
                    for (int i = 0; i < len; ++i)
                        std::cout << std::hex << int(buf[i]) << (i + 1 < len ? " " : "");
                    std::cout << std::dec << "]\n";
                }
                catch (const std::exception & e)
                {
                    std::cout << "rejected: " << e.what() << "\n";
                }
                if (pr.try_set)
                {
                    // write back exactly what was read (no-op value change)
                    uint8_t wbuf[16] = {};
                    if (pr.ctrl == 6)
                    {
                        uint8_t cur[16] = {};
                        try { rsimpl::uvc::get_control(uvc, rsimpl::ds::lr_xu, pr.ctrl, cur, len); }
                        catch (...) { continue; }
                        std::memcpy(wbuf, cur, len);
                    }
                    std::cout << "SET ctrl " << pr.ctrl << " (" << pr.name << ") len " << len
                              << (pr.ctrl == 6 ? " readback: " : " val0: ");
                    try
                    {
                        rsimpl::uvc::set_control(uvc, rsimpl::ds::lr_xu, pr.ctrl, wbuf, len);
                        std::cout << "ok\n";
                    }
                    catch (const std::exception & e)
                    {
                        std::cout << "rejected: " << e.what() << "\n";
                    }
                    if (pr.ctrl == 3)
                    {
                        uint8_t one[4] = { 1, 0, 0, 0 };
                        std::cout << "SET ctrl 3 (stream_intent) len " << len << " val1: ";
                        try
                        {
                            rsimpl::uvc::set_control(uvc, rsimpl::ds::lr_xu, pr.ctrl, one, len);
                            std::cout << "ok\n";
                        }
                        catch (const std::exception & e)
                        {
                            std::cout << "rejected: " << e.what() << "\n";
                        }
                        uint8_t back[4] = {};
                        try { rsimpl::uvc::set_control(uvc, rsimpl::ds::lr_xu, pr.ctrl, back, len); } catch (...) {}
                    }
                }
            }
        }
        // restore emitter state to what it was when this run started
        try { rsimpl::uvc::set_control(uvc, rsimpl::ds::lr_xu, 8, &emitter_initial, 1); }
        catch (...) {}
        rs_delete_context(context, &error);
        return 0;
    }

    if (mode == "handshake")
    {
        // Benign opcode/size probing: only query-class opcodes are sent here.
        // Dangerous opcodes (0x12 poke, 0x19 write, 0x1B erase) are excluded.
        // 0x11 peek is also excluded: an oversized peek packet crashed the
        // firmware once and forced a boot-bank fallback (2026-08-23 incident).
        const uint32_t opcodes[] = { 0x15, 0x1C, 0x21, 0x22 };
        const uint32_t sizes[] = { 0xDC, 0xEC, 0x100 };
        for (uint32_t code : opcodes)
        {
            for (uint32_t len : sizes)
            {
                std::vector<uint8_t> buf(len, 0);
                uint32_t mod = mod_direct;
                std::memcpy(buf.data(), &code, 4);
                std::memcpy(buf.data() + 4, &mod, 4);
                std::cout << "op 0x" << std::hex << code << " len 0x" << len << std::dec << ": ";
                try
                {
                    rsimpl::uvc::set_control(uvc, rsimpl::ds::lr_xu,
                                             static_cast<int>(rsimpl::ds::control::command_response),
                                             buf.data(), int(len));
                    std::cout << "ACCEPTED\n";
                }
                catch (const std::exception & e)
                {
                    std::cout << "rejected: " << e.what() << "\n";
                }
            }
        }
        rs_delete_context(context, &error);
        return 0;
    }

    if (mode == "read")
    {
        std::vector<uint8_t> buf(total);
        if (!read_back(uvc, address, buf.data(), n_sectors * pages_per_sector))
            throw std::runtime_error("flash read rejected");
        std::cout << "read 0x" << std::hex << address << " len 0x" << total << std::dec << " ok\n";
        hexdump("first 32 bytes", buf.data(), 32);
        if (argc >= 5)
        {
            auto ref = read_file(argv[4], flash_size);
            bool same = std::memcmp(buf.data(), ref.data() + address, total) == 0;
            std::cout << "compare with reference: " << (same ? "IDENTICAL" : "DIFFERENT") << "\n";
            if (!same)
                for (uint32_t s = 0; s < n_sectors; ++s)
                    if (std::memcmp(buf.data() + s * sector_size, ref.data() + address + s * sector_size, sector_size))
                        std::cout << "  sector differs: 0x" << std::hex << address + s * sector_size << std::dec << "\n";
        }
        rs_delete_context(context, &error);
        return 0;
    }

    if (main_pilot_mode)
    {
        if (!commit)
            throw std::runtime_error("main pilot refused: add --commit");
        const char * confirm = std::getenv("R200_MAIN_PILOT_CONFIRM");
        if (!confirm || std::string(confirm) != "RUNNING_BACKUP_BANK_SECTOR_10_ONLY")
            throw std::runtime_error(
                "main pilot refused: set R200_MAIN_PILOT_CONFIRM=RUNNING_BACKUP_BANK_SECTOR_10_ONLY");
        const std::string device_name = rs_get_device_name(public_device, &error);
        const std::string firmware = rs_get_device_firmware_version(public_device, &error);
        check(error);
        if (device_name.find("LR200") == std::string::npos || firmware != "2.0.71.04")
            throw std::runtime_error("main pilot requires target running LR200 2.0.71.04 backup image");

        const auto source = read_file(argv[2], flash_size);
        const auto expected = read_file(argv[3], flash_size);
        std::vector<uint8_t> before(sector_size);
        if (!read_back(uvc, address, before.data(), pages_per_sector) ||
            std::memcmp(before.data(), expected.data() + address, sector_size) != 0)
            throw std::runtime_error("main pilot connected-sector mismatch; refusing");
        const uint8_t * wanted = source.data() + address;
        if (std::memcmp(before.data(), wanted, sector_size) == 0)
            throw std::runtime_error("main pilot source already matches; nothing to test");

        std::cout << "validated LR200 backup runtime and exact current sector 0x10000\n";
        erase_sector(uvc, address, true);
        write_pages(uvc, address, wanted, pages_per_sector, true);
        std::vector<uint8_t> verify(sector_size);
        if (!read_back(uvc, address, verify.data(), pages_per_sector) ||
            std::memcmp(verify.data(), wanted, sector_size) != 0)
            throw std::runtime_error("main pilot VERIFY FAILED; STOP");
        std::cout << "main pilot sector 0x10000 verify ok; no other sector written\n";
        rs_delete_context(context, &error);
        return 0;
    }

    if (a1_hybrid_mode)
    {
        if (!commit)
            throw std::runtime_error("A1 hybrid write refused: add --commit");
        const char * confirm = std::getenv("R200_A1_HYBRID_CONFIRM");
        if (!confirm || std::string(confirm) != "ORIGINAL_LR200_SHIFT2D")
            throw std::runtime_error(
                "A1 hybrid write refused: set R200_A1_HYBRID_CONFIRM=ORIGINAL_LR200_SHIFT2D");
        const std::string device_name = rs_get_device_name(public_device, &error);
        const std::string firmware = rs_get_device_firmware_version(public_device, &error);
        check(error);
        if (device_name.find("R200") == std::string::npos || firmware != "1.0.72.10")
            throw std::runtime_error("A1 hybrid write requires target running R200 1.0.72.10");

        const auto source = read_file(argv[2], flash_size);
        const auto expected = read_file(argv[3], flash_size);
        if (std::memcmp(source.data(), expected.data(), address) != 0 ||
            std::memcmp(source.data() + address + sector_size,
                        expected.data() + address + sector_size,
                        flash_size - address - sector_size) != 0)
            throw std::runtime_error("A1 hybrid source differs outside sector 0xA1; refusing");
        const uint8_t version2[4] = { 0, 0, 0, 2 };
        const uint8_t trailer[4] = { 0x0A, 0, 0, 0 };
        if (std::memcmp(source.data() + address, version2, 4) != 0 ||
            std::memcmp(source.data() + address + 0x804, "\x1F\0\0\0", 4) != 0 ||
            std::memcmp(source.data() + address + 0xFFC, trailer, 4) != 0)
            throw std::runtime_error("A1 hybrid structural validation failed");

        std::vector<uint8_t> before(sector_size);
        if (!read_back(uvc, address, before.data(), pages_per_sector) ||
            std::memcmp(before.data(), expected.data() + address, sector_size) != 0)
            throw std::runtime_error("connected target A1 does not match expected-current image; refusing");

        std::cout << "validated one-sector A1 hybrid operation\n";
        erase_sector_known_nv(uvc, address);
        write_pages_known_nv(uvc, address, source.data() + address, pages_per_sector);
        std::vector<uint8_t> verify(sector_size);
        if (!read_back(uvc, address, verify.data(), pages_per_sector) ||
            std::memcmp(verify.data(), source.data() + address, sector_size) != 0)
            throw std::runtime_error("A1 hybrid VERIFY FAILED; STOP");
        std::cout << "A1 hybrid verify ok; no other sector written\n";
        rs_delete_context(context, &error);
        return 0;
    }

    if (a3_candidate_mode)
    {
        if (!commit)
            throw std::runtime_error("A3 candidate write refused: add --commit");
        const char * confirm = std::getenv("R200_A3_CANDIDATE_CONFIRM");
        if (!confirm || std::string(confirm) != "ORIGINAL_LR200_SHIFT2D_A3_ONLY")
            throw std::runtime_error(
                "A3 candidate write refused: set R200_A3_CANDIDATE_CONFIRM=ORIGINAL_LR200_SHIFT2D_A3_ONLY");
        const std::string device_name = rs_get_device_name(public_device, &error);
        const std::string firmware = rs_get_device_firmware_version(public_device, &error);
        check(error);
        if (device_name.find("R200") == std::string::npos || firmware != "1.0.72.10")
            throw std::runtime_error("A3 candidate write requires target running R200 1.0.72.10");

        const auto source = read_file(argv[2], flash_size);
        const auto expected = read_file(argv[3], flash_size);
        if (std::memcmp(source.data(), expected.data(), address) != 0 ||
            std::memcmp(source.data() + address + sector_size,
                        expected.data() + address + sector_size,
                        flash_size - address - sector_size) != 0)
            throw std::runtime_error("A3 candidate source differs outside sector 0xA3; refusing");

        // The first 0x400 payload bytes are common between the old LR200,
        // packaged IFFLEY image and donor R200.  The candidate changes only
        // the later record copies and preserves the R200 slot footer.
        const uint8_t trailer[4] = { 0x01, 0, 0, 0 };
        if (std::memcmp(source.data() + address, expected.data() + address, 0x400) != 0 ||
            std::memcmp(source.data() + address + 0xFFC, trailer, 4) != 0)
            throw std::runtime_error("A3 candidate structural validation failed");

        std::vector<uint8_t> before(sector_size);
        if (!read_back(uvc, address, before.data(), pages_per_sector) ||
            std::memcmp(before.data(), expected.data() + address, sector_size) != 0)
            throw std::runtime_error("connected target A3 does not match expected-current image; refusing");

        std::cout << "validated one-sector A3 candidate operation\n";
        erase_sector_known_nv(uvc, address);
        write_pages_known_nv(uvc, address, source.data() + address, pages_per_sector);
        std::vector<uint8_t> verify(sector_size);
        if (!read_back(uvc, address, verify.data(), pages_per_sector) ||
            std::memcmp(verify.data(), source.data() + address, sector_size) != 0)
            throw std::runtime_error("A3 candidate VERIFY FAILED; STOP");
        std::cout << "A3 candidate verify ok; no other sector written\n";
        rs_delete_context(context, &error);
        return 0;
    }

    if (backup_bank_mode)
    {
        if (!commit)
            throw std::runtime_error("backup-bank write refused: add --commit");
        const char * confirm = std::getenv("R200_BACKUP_BANK_WRITE_CONFIRM");
        if (!confirm || std::string(confirm) != "TARGET_BACKUP_BANK_ONLY")
            throw std::runtime_error(
                "backup-bank write refused: set R200_BACKUP_BANK_WRITE_CONFIRM=TARGET_BACKUP_BANK_ONLY");

        const auto source = read_file(argv[2], flash_size);
        const auto expected = read_file(argv[3], flash_size);
        if (std::memcmp(source.data(), expected.data(), address) != 0 ||
            std::memcmp(source.data() + address + total,
                        expected.data() + address + total,
                        flash_size - address - total) != 0)
            throw std::runtime_error(
                "source and expected-current differ outside [0x50000,0x80000); refusing");

        std::vector<uint8_t> before(total);
        if (!read_back(uvc, address, before.data(), n_sectors * pages_per_sector))
            throw std::runtime_error("backup-bank pre-write read-back failed");
        if (std::memcmp(before.data(), expected.data() + address, total) != 0)
            throw std::runtime_error(
                "connected device backup bank does not match expected-current image; refusing");

        uint32_t changed = 0;
        for (uint32_t s = 0; s < n_sectors; ++s)
            if (std::memcmp(before.data() + s * sector_size,
                            source.data() + address + s * sector_size,
                            sector_size) != 0)
                ++changed;
        std::cout << "validated exact backup-bank operation; differing sectors=" << changed << "\n";
        if (!changed)
        {
            std::cout << "backup bank already matches source; nothing written\n";
            rs_delete_context(context, &error);
            return 0;
        }

        uint32_t written = 0;
        for (uint32_t s = 0; s < n_sectors; ++s)
        {
            const uint8_t * wanted = source.data() + address + s * sector_size;
            if (std::memcmp(before.data() + s * sector_size, wanted, sector_size) == 0)
                continue;
            const uint32_t a = address + s * sector_size;
            std::cout << "== backup-bank sector 0x" << std::hex << a << std::dec << " ==\n";
            erase_sector(uvc, a, true);
            write_pages(uvc, a, wanted, pages_per_sector, true);
            std::vector<uint8_t> verify(sector_size);
            if (!read_back(uvc, a, verify.data(), pages_per_sector))
                throw std::runtime_error("backup-bank post-write read-back failed");
            if (std::memcmp(verify.data(), wanted, sector_size) != 0)
                throw std::runtime_error("backup-bank VERIFY FAILED; STOP");
            std::cout << "  verify ok\n";
            ++written;
            if (backup_bank_pilot)
            {
                std::cout << "pilot complete after one differing sector; remaining sectors untouched\n";
                break;
            }
        }

        if (!backup_bank_pilot)
        {
            std::vector<uint8_t> final_bank(total);
            if (!read_back(uvc, address, final_bank.data(), n_sectors * pages_per_sector) ||
                std::memcmp(final_bank.data(), source.data() + address, total) != 0)
                throw std::runtime_error("final backup-bank verification failed");
            std::cout << "backup-bank write complete; entire [0x50000,0x80000) matches source\n";
        }
        rs_delete_context(context, &error);
        return 0;
    }

    // write mode
    if (address < allowed_lo || address + total > allowed_hi)
        throw std::runtime_error("write refused: range must be inside [0xA0000, 0x100000)");
    if (!commit)
        throw std::runtime_error("write refused: add --commit to actually write (probe first!)");
    const char * confirm = std::getenv("R200_WRITE_CONFIRM");
    if (!confirm || std::string(confirm) != "I_UNDERSTAND")
        throw std::runtime_error("write refused: set R200_WRITE_CONFIRM=I_UNDERSTAND to confirm");

    auto content = read_file(argv[4], flash_size);
    const uint8_t * src = content.data() + address;

    // Pre-write read-back sanity.
    std::vector<uint8_t> before(total);
    if (!read_back(uvc, address, before.data(), n_sectors * pages_per_sector))
        throw std::runtime_error("pre-write read-back failed");

    for (uint32_t s = 0; s < n_sectors; ++s)
    {
        uint32_t a = address + s * sector_size;
        std::cout << "== sector 0x" << std::hex << a << std::dec << " ==\n";
        erase_sector(uvc, a, true);
        write_pages(uvc, a, src + s * sector_size, pages_per_sector, true);

        std::vector<uint8_t> verify(sector_size);
        if (!read_back(uvc, a, verify.data(), pages_per_sector))
            throw std::runtime_error("post-write read-back failed");
        if (std::memcmp(verify.data(), src + s * sector_size, sector_size) != 0)
            throw std::runtime_error("VERIFY FAILED at sector 0x" + std::to_string(a) + " - STOP");
        std::cout << "  verify ok\n";
    }

    std::cout << "write complete; re-run 'read' mode for a full re-check\n";
    rs_delete_context(context, &error);
    return 0;
}
catch (const std::exception & e)
{
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
}

#include <kernel/ps3/loader.hpp>
#include <kernel/ps3/powerpc/decode.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>

namespace notyvos::ps3 {

namespace {

struct DecodeCase {
    u32 word;
    const char* expect;
};

} // namespace

void self_test() noexcept {
    log::write(log::Level::Info, "ps3", "running self-test");

    const DecodeCase cases[] = {
        { 0x38600042u, "addi" },
        { 0x48000008u, "b" },
        { 0x4E800020u, "bclr" },
        { 0x7C0802A6u, "mfspr" },
        { 0x7C0803A6u, "mtspr" },
        { 0x9421FFE0u, "stwu" },
        { 0x38210020u, "addi" },
        { 0x4E800421u, "bcctr" },
    };

    u32 ok = 0;
    for (const auto& c : cases) {
        const auto ins = powerpc::decode(c.word);
        if (ins.mnemonic && libk::strcmp(ins.mnemonic, c.expect) == 0) {
            ++ok;
        } else {
            log::write(log::Level::Warn, "ps3-dec",
                "0x%llx decoded as '%s', expected '%s'",
                static_cast<unsigned long long>(c.word),
                ins.mnemonic ? ins.mnemonic : "(null)",
                c.expect);
        }
    }
    log::write(log::Level::Info, "ps3-dec",
        "self-test: %u/%u instructions recognised",
        static_cast<unsigned long long>(ok),
        static_cast<unsigned long long>(sizeof(cases) / sizeof(cases[0])));

    static const u8 fake_elf[64] = {
        0x7F, 'E', 'L', 'F', 2, 2, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0x00, 0x02,
        0x00, 0x15,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x40,
        0x00, 0x38,
        0x00, 0x02,
        0x00, 0x40, 0x00, 0x00,
        0x00, 0x00,
    };

    if (!is_ps3_executable(fake_elf, sizeof(fake_elf))) {
        log::write(log::Level::Warn, "ps3", "self-test: fake ELF rejected");
        return;
    }

    Ps3Program prog{};
    if (!parse_ps3_executable(fake_elf, sizeof(fake_elf), &prog)) {
        log::write(log::Level::Warn, "ps3", "self-test: parse failed");
        return;
    }

    log::write(log::Level::Info, "ps3",
        "self-test: parsed entry=0x%llx, %u segments declared",
        static_cast<unsigned long long>(prog.entry),
        static_cast<unsigned long long>(prog.phnum));
}

} // namespace notyvos::ps3
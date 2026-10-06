/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "audio-symbols.hpp"
#include <elf.h>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace {
bool read_at(std::ifstream &file, uint64_t size, uint64_t offset, void *output, size_t bytes) {
    if (offset > size || bytes > size - offset) {
        return false;
    }
    file.seekg(offset);
    return static_cast<bool>(file.read(static_cast<char *>(output), bytes));
}
}

AudioSymbols resolve_audio_symbols(const std::string &path, uintptr_t base) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const auto end = file.tellg();
    if (end < 0) {
        return {};
    }
    const uint64_t size = static_cast<uint64_t>(end);
    Elf64_Ehdr header{};
    if (!read_at(file, size, 0, &header, sizeof(header))
        || std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0
        || header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB
        || header.e_machine != EM_X86_64 || header.e_type != ET_DYN
        || header.e_shentsize != sizeof(Elf64_Shdr) || header.e_shnum == 0) {
        return {};
    }
    std::vector<Elf64_Shdr> sections(header.e_shnum);
    if (!read_at(
            file, size, header.e_shoff, sections.data(), sections.size() * sizeof(Elf64_Shdr))) {
        return {};
    }
    constexpr const char *names[] = {
        "_ZN7discord5media10soundshare20PulseAudioController15OnSinkInputInfoEP10pa_contextPK18pa_"
        "sink_input_infoiPv",
        "_ZNK7discord5media10soundshare20PulseAudioController11AttachedPidEv",
        "_Z19GetPulseSymbolTablev"};
    std::array<uintptr_t, 3> addresses{};
    std::array<unsigned, 3> matches{};
    for (const auto &section : sections) {
        if (section.sh_type != SHT_SYMTAB || section.sh_entsize != sizeof(Elf64_Sym)
            || section.sh_size % sizeof(Elf64_Sym) != 0 || section.sh_link >= sections.size()) {
            continue;
        }
        const auto &strings_section = sections[section.sh_link];
        if (strings_section.sh_type != SHT_STRTAB || section.sh_offset > size
            || section.sh_size > size - section.sh_offset || strings_section.sh_offset > size
            || strings_section.sh_size > size - strings_section.sh_offset) {
            return {};
        }
        std::vector<Elf64_Sym> symbols(section.sh_size / sizeof(Elf64_Sym));
        std::vector<char> strings(strings_section.sh_size);
        if (!read_at(file, size, section.sh_offset, symbols.data(), section.sh_size)
            || !read_at(file, size, strings_section.sh_offset, strings.data(), strings.size())) {
            return {};
        }
        for (const auto &symbol : symbols) {
            if (ELF64_ST_TYPE(symbol.st_info) != STT_FUNC || symbol.st_shndx >= sections.size()
                || symbol.st_shndx == SHN_UNDEF || symbol.st_name >= strings.size()) {
                continue;
            }
            const char *name = strings.data() + symbol.st_name;
            if (!std::memchr(name, '\0', strings.size() - symbol.st_name)) {
                return {};
            }
            for (size_t i = 0; i < addresses.size(); ++i) {
                if (std::strcmp(name, names[i]) != 0) {
                    continue;
                }
                const auto &code = sections[symbol.st_shndx];
                if ((code.sh_flags & (SHF_ALLOC | SHF_EXECINSTR)) != (SHF_ALLOC | SHF_EXECINSTR)
                    || symbol.st_value < code.sh_addr
                    || symbol.st_value - code.sh_addr >= code.sh_size
                    || symbol.st_size > code.sh_size - (symbol.st_value - code.sh_addr)
                    || symbol.st_value > std::numeric_limits<uintptr_t>::max() - base) {
                    return {};
                }
                addresses[i] = base + symbol.st_value;
                ++matches[i];
            }
        }
    }
    if (matches[0] != 1 || matches[1] != 1 || matches[2] != 1) {
        return {};
    }
    return {addresses[0], addresses[1], addresses[2]};
}

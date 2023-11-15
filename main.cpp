#include <iostream>
#include <fstream>
#include <cassert>
#include <vector>
#include <string>

#include <mach-o/loader.h>
#include <mach-o/swap.h>
#include <mach-o/fat.h>

class MachOParser {
public:
    MachOParser(const std::string& filename) : filename_(filename) {
        obj_file_.open(filename, std::ios::binary);
        if (!obj_file_.is_open()) {
            std::cerr << "Failed to open the file: " << filename << std::endl;
            assert(false);
        }
    }

    ~MachOParser() {
        if (obj_file_.is_open()) {
            obj_file_.close();
        }
    }

    void Parse() {
        uint32_t magic = ReadMagic(0);
        std::cout << "Magic: " << magic << std::endl;

        bool is_64 = IsMagic64(magic);
        std::cout << "Is 64-bit: " << is_64 << std::endl;

        bool is_swap = IsLittleEndianMagic(magic);
        std::cout << "Is Little Endian: " << is_swap << std::endl;

        bool is_fat = IsFat(magic);
        std::cout << "Is Fat: " << is_fat << std::endl;

        if (is_fat) {
            std::cout << "Parsing Fat Mach-O" << std::endl;
            ParseFatHeader(is_swap);
        } else {
            std::cout << "Parsing Single Mach-O" << std::endl;
            ParseMachHeader(0, is_64, is_swap);
        }
    }

private:
    std::string filename_;
    std::ifstream obj_file_;

    const char* CpuTypeName(cpu_type_t cpu_type) {
        switch (cpu_type) {
            case CPU_TYPE_I386:
                return "i386";
            case CPU_TYPE_X86_64:
                return "x86_64";
            case CPU_TYPE_ARM:
                return "arm";
            case CPU_TYPE_ARM64:
                return "arm64";
            default:
                return "unknown";
        }
    } 

    uint32_t ReadMagic(off_t offset) {
        uint32_t magic;
        obj_file_.seekg(offset, std::ios::beg);
        obj_file_.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
        return magic;
    }

    bool IsMagic64(uint32_t magic) {
        return magic == MH_MAGIC_64 || magic == MH_CIGAM_64;
    }

    bool IsLittleEndianMagic(uint32_t magic) {
        return magic == MH_CIGAM || magic == MH_CIGAM_64 || magic == FAT_CIGAM;
    }

    bool IsFat(uint32_t magic) {
        return magic == FAT_MAGIC || magic == FAT_CIGAM;
    }

    void* LoadBytes(off_t offset, size_t size) {
        void* buf = calloc(1, size);
        obj_file_.seekg(offset, std::ios::beg);
        obj_file_.read(reinterpret_cast<char*>(buf), size);
        return buf;
    }

    void ParseSegmentsCommands(off_t offset, bool is_swap, uint32_t ncmds) {
        off_t actual_offset = offset;
        std::cout << "Number of Load Commands: " << ncmds << std::endl;

        for (uint32_t i = 0; i < ncmds; i++) {
            struct load_command* cmd = reinterpret_cast<struct load_command*>(LoadBytes(actual_offset, sizeof(struct load_command)));

            if (is_swap) {
                swap_load_command(cmd, (NXByteOrder)0);
            }

            if (cmd->cmd == LC_SEGMENT_64) {
                std::cout << "Load Command: LC_SEGMENT_64" << std::endl;
                struct segment_command_64* segment = reinterpret_cast<struct segment_command_64*>(LoadBytes(actual_offset, sizeof(struct segment_command_64)));
                if (is_swap) {
                    swap_segment_command_64(segment, (NXByteOrder)0);
                }
                std::cout << "Segment Name: " << segment->segname << std::endl;
                free(segment);
            } else if (cmd->cmd == LC_SEGMENT) {
                std::cout << "Load Command: LC_SEGMENT" << std::endl;
                struct segment_command* segment = reinterpret_cast<struct segment_command*>(LoadBytes(actual_offset, sizeof(struct segment_command)));
                if (is_swap) {
                    swap_segment_command(segment, (NXByteOrder)0);
                }
                std::cout << "Segment Name: " << segment->segname << std::endl;
                free(segment);
            }
            actual_offset += cmd->cmdsize;
            free(cmd);
        }
    }

    void ParseMachHeader(off_t offset, bool is_64, bool is_swap) {
        uint32_t ncmds;
        off_t load_command_offset = offset;

        if (is_64) {
            size_t header_size = sizeof(struct mach_header_64);
            struct mach_header_64* header = reinterpret_cast<struct mach_header_64*>(LoadBytes(offset, header_size));
            if (is_swap) {
                swap_mach_header_64(header, (NXByteOrder)0);
            }

            ncmds = header->ncmds;
            std::cout << "Number of Load Commands: " << ncmds << std::endl;
            load_command_offset += header_size;
            std::cout << "CPU Type Name: " << CpuTypeName(header->cputype) << std::endl;
            free(header);
        } else {
            size_t header_size = sizeof(struct mach_header);
            struct mach_header* header = reinterpret_cast<struct mach_header*>(LoadBytes(offset, header_size));
            if (is_swap) {
                swap_mach_header(header, (NXByteOrder)0);
            }

            ncmds = header->ncmds;
            std::cout << "Number of Load Commands: " << ncmds << std::endl;
            load_command_offset += header_size;
            std::cout << "CPU Type Name: " << CpuTypeName(header->cputype) << std::endl;
            free(header);
        }

        ParseSegmentsCommands(load_command_offset, is_swap, ncmds);
    }

    void ParseFatHeader(bool is_swap) {
        size_t header_size = sizeof(struct fat_header);
        size_t arch_size = sizeof(struct fat_arch);

        struct fat_header* header = reinterpret_cast<struct fat_header*>(LoadBytes(0, header_size));
        if (is_swap) {
            swap_fat_header(header, (NXByteOrder)0);
        }

        off_t arch_offset = static_cast<off_t>(header_size);
        for (uint32_t i = 0; i < header->nfat_arch; i++) {
            struct fat_arch* arch = reinterpret_cast<struct fat_arch*>(LoadBytes(arch_offset, arch_size));

            if (is_swap) {
                swap_fat_arch(arch, 1, (NXByteOrder)0);
            }

            off_t mach_header_offset = static_cast<off_t>(arch->offset);
            free(arch);
            arch_offset += arch_size;
            uint32_t magic = ReadMagic(mach_header_offset);
            bool is_64 = IsMagic64(magic);
            bool is_swap_mach = IsLittleEndianMagic(magic);
            ParseMachHeader(mach_header_offset, is_64, is_swap_mach);
        }
        free(header);
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mach-o 파일>" << std::endl;
        return 1;
    }

    const std::string filename = argv[1];
    MachOParser parser(filename);
    parser.Parse();
    std::cout << "End" << std::endl;

    return 0;
}
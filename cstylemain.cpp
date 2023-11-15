#include <iostream>
#include <cstdlib>
#include <mach-o/loader.h>
#include <mach-o/swap.h>
#include <mach-o/fat.h>
// C 스타일
using namespace std;

void DumpSegments(FILE *obj_file);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        cout << "Usage: <mach-o 파일>" << endl;
        return 1;
    }
    const char *filename = argv[1];
    FILE *obj_file = fopen(filename, "rb"); //"rb"옵션 바이너리 파일로 열기
    if (obj_file == NULL) {
        cout << "file is not open" << endl;
        return 1;
    }
    DumpSegments(obj_file);
    fclose(obj_file);
    cout << "end";
    return 0;
}

uint32_t ReadMagic(FILE *obj_file, off_t offset) {
    uint32_t magic;
    fseek(obj_file, offset, SEEK_SET); //파일을 읽을때마다 _offset파일 내부가 변경
    fread(&magic, sizeof(uint32_t), 1, obj_file);
    return magic;
}

int IsMagic64(uint32_t magic) {
    return magic == MH_MAGIC_64 || magic == MH_CIGAM_64;
}

int IsLittleEndianMagic(uint32_t magic) { //리틀엔디안 or 빅엔디안
    return magic == MH_CIGAM || magic == MH_CIGAM_64 || magic == FAT_CIGAM;
}

int IsFat(uint32_t magic) {
    return magic == FAT_MAGIC || magic == FAT_CIGAM;
}

struct _cpu_type_names {
    cpu_type_t cputype;
    char *cpu_name;
};

struct _cpu_type_names cpu_type_names[] = {
    { CPU_TYPE_I386, "i386" },
    { CPU_TYPE_X86_64, "x86_64" },
    { CPU_TYPE_ARM, "arm" },
    { CPU_TYPE_ARM64, "arm64" }
};

char *CpuTypeName(cpu_type_t cpu_type) {
    int cpu_type_names_size = sizeof(cpu_type_names) / sizeof(struct _cpu_type_names);
    for(int i = 0; i < cpu_type_names_size; i++) {
        if (cpu_type == cpu_type_names[i].cputype) {
            return cpu_type_names[i].cpu_name;
        }
    }

    return "unknown";
}

void *LoadBytes(FILE *obj_file, off_t offset, size_t size) {
    void *buf = calloc(1, size);
    fseek(obj_file, offset, SEEK_SET);
    fread(buf, size, 1, obj_file);

    return buf;
}


void DumpSegmentsCommands(FILE *obj_file, off_t offset, int is_swap, uint32_t ncmds) {
    off_t actual_offset = offset;
    cout << "ncmds: " << ncmds << endl;
    
    for (uint32_t i = 0U; i < ncmds; i++) {
        struct load_command *cmd = (struct load_command *)LoadBytes(obj_file, actual_offset, sizeof(struct load_command));
        
        if (is_swap) {
            swap_load_command(cmd, (NXByteOrder)0);
        }
        if (cmd->cmd == LC_SEGMENT_64) { //LC_SEGMENT_64 세그먼트 주소
            cout << "cmd: LC_SEGMENT_64/ ";
            struct segment_command_64 *segment = (struct segment_command_64 *)LoadBytes(obj_file, actual_offset, sizeof(struct segment_command_64));
            if (is_swap) {
                swap_segment_command_64(segment, (NXByteOrder)0);
            }
            cout << "segname: " << segment->segname << endl;
            free(segment);
        } else if (cmd->cmd == LC_SEGMENT) {
            cout << "cmd: LC_SEGMENT/ ";
            struct segment_command *segment = (struct segment_command *)LoadBytes(obj_file, actual_offset, sizeof(struct segment_command));
            if (is_swap) {
                swap_segment_command(segment, (NXByteOrder)0);
            }
            cout << "segname: " << segment->segname << endl;
            free(segment);
        }
        actual_offset += cmd->cmdsize;
        free(cmd);
    }
}

void DumpMachHeader(FILE *obj_file, off_t offset, int is_64, int is_swap) {
    uint32_t ncmds;
    off_t load_command_offset = offset;
    if (is_64) {
        cout << "is_64" << is_64 << endl;
        size_t header_size = sizeof(struct mach_header_64);
        struct mach_header_64 *header = (struct mach_header_64 *)LoadBytes(obj_file, offset, header_size);
        if (is_swap) {
            swap_mach_header_64(header, (NXByteOrder)0); //#include <mach-o/swap.h>
        }

        ncmds = header->ncmds;
        cout << "ncmds: " << ncmds << endl;

        load_command_offset += header_size;
        cout << "CpuTypeName: " << CpuTypeName(header->cputype) << endl;
        free(header);
    } else {
        cout << "else" << endl;
        size_t header_size = sizeof(struct mach_header);
        struct mach_header *header = (struct mach_header*)LoadBytes(obj_file, offset, header_size);
        if (is_swap) {
            swap_mach_header(header, (NXByteOrder)0);
        }

        ncmds = header->ncmds;
        cout << "header->ncmds: " << header->ncmds << endl;
        load_command_offset += header_size;
        cout << "header_size: " << header_size << endl;
        cout << "cpu_type_name: " << header->cputype << endl;
        cout << "CpuSubType: " << header->cpusubtype << endl;
        free(header);
    }
    DumpSegmentsCommands(obj_file, load_command_offset, is_swap, ncmds);
}

void DumpFatHeader(FILE *obj_file, int is_swap) {
    size_t header_size = sizeof(struct fat_header);
    size_t arch_size = sizeof(struct fat_arch);

    struct fat_header *header = (struct fat_header *)LoadBytes(obj_file, 0, header_size);
    if (is_swap) {
        swap_fat_header(header, (NXByteOrder)0);
    }

    off_t arch_offset = (off_t)header_size;
    for (uint32_t i = 0U; i < header->nfat_arch; i++) {
        struct fat_arch *arch = (struct fat_arch *)LoadBytes(obj_file, arch_offset, arch_size);

        if (is_swap) {
            swap_fat_arch(arch, 1, (NXByteOrder)0);
        }

        off_t mach_header_offset = (off_t)arch->offset;
        free(arch);
        arch_offset += arch_size;
        uint32_t magic = ReadMagic(obj_file, mach_header_offset);
        int is_64 = IsMagic64(magic);
        int is_swap_mach = IsLittleEndianMagic(magic);
        DumpMachHeader(obj_file, mach_header_offset, is_64, is_swap_mach);
    }
    free(header);
}

void DumpSegments(FILE *obj_file) { 
    uint32_t magic = ReadMagic(obj_file, 0);
    cout << "magic: " << magic << endl;

    int is_64 = IsMagic64(magic);
    cout << "isMagic64: " << is_64 << endl;

    int is_swap = IsLittleEndianMagic(magic);
    cout << "IsLittleEndianMagic: " << is_swap << endl;

    int is_fat = IsFat(magic);
    cout << "isFat: " << is_fat << endl; 
    if (is_fat) { //0 = false
        cout << "DumpFatHeader" << endl;
        DumpFatHeader(obj_file, is_swap);
    }
    else{
        cout << "DumpMachHeader" << endl;
        DumpMachHeader(obj_file, 0, is_64, is_swap);
    }
}
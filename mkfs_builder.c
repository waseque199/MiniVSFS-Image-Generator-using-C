// Build: gcc -O2 -std=c17 -Wall -Wextra mkfs_builder.c -o mkfs_builder
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#define BS 4096u               // block size
#define INODE_SIZE 128u
#define ROOT_INO 1u

uint64_t g_random_seed = 0; // This should be replaced by seed value from the CLI.

// below contains some basic structures you need for your project
// you are free to create more structures as you require

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;               // 0x4D565346
    uint32_t version;             // 1
    uint32_t block_size;          // 4096
    uint64_t total_blocks;        // calculated from size_kib
    uint64_t inode_count;         // from CLI
    uint64_t inode_bitmap_start;  // 1
    uint64_t inode_bitmap_blocks; // 1
    uint64_t data_bitmap_start;   // 2
    uint64_t data_bitmap_blocks;  // 1
    uint64_t inode_table_start;   // 3
    uint64_t inode_table_blocks;  // calculated
    uint64_t data_region_start;   // calculated
    uint64_t data_region_blocks;  // calculated
    uint64_t root_inode;          // 1
    uint64_t mtime_epoch;         // build time
    uint32_t flags;               // 0
    
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint32_t checksum;            // crc32(superblock[0..4091])
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    uint16_t mode;                // file/directory mode
    uint16_t links;               // link count
    uint32_t uid;                 // 0
    uint32_t gid;                 // 0
    uint64_t size_bytes;          // file size
    uint64_t atime;               // access time
    uint64_t mtime;               // modify time
    uint64_t ctime;               // create time
    uint32_t direct[12];          // direct block pointers
    uint32_t reserved_0;          // 0
    uint32_t reserved_1;          // 0
    uint32_t reserved_2;          // 0
    uint32_t proj_id;             // 3 (your group ID)
    uint32_t uid16_gid16;         // 0
    uint64_t xattr_ptr;           // 0

    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint64_t inode_crc;   // low 4 bytes store crc32 of bytes [0..119]; high 4 bytes 0

} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;            // inode number (0 if free)
    uint8_t type;                 // 1=file, 2=dir
    char name[58];                // filename/dirname

    uint8_t  checksum; // XOR of bytes 0..62
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent size mismatch");


// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
uint32_t CRC32_TAB[256];
void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}
// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *) sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; memcpy(tmp, ino, INODE_SIZE);
    // zero crc area before computing
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c; // low 4 bytes carry the crc
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];   
    de->checksum = x;
}

void print_usage(const char* prog_name) {
    printf("Usage: %s --image <filename> --size-kib <180..4096> --inodes <128..512>\n", prog_name);
}

int parse_args(int argc, char* argv[], char** image_name, uint64_t* size_kib, uint64_t* inodes) {
    if (argc != 7) {
        return -1;
    }
    
    *image_name = NULL;
    *size_kib = 0;
    *inodes = 0;
    
    for (int i = 1; i < argc; i += 2) {
        if (strcmp(argv[i], "--image") == 0) {
            *image_name = argv[i + 1];
        } else if (strcmp(argv[i], "--size-kib") == 0) {
            *size_kib = strtoull(argv[i + 1], NULL, 10);
        } else if (strcmp(argv[i], "--inodes") == 0) {
            *inodes = strtoull(argv[i + 1], NULL, 10);
        } else {
            return -1;
        }
    }
    

    if (*image_name == NULL || *size_kib < 180 || *size_kib > 4096 || 
        *inodes < 128 || *inodes > 512 || (*size_kib % 4) != 0) {
        return -1;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    crc32_init();
    

    char* image_name;
    uint64_t size_kib;
    uint64_t inodes;
    
    if (parse_args(argc, argv, &image_name, &size_kib, &inodes) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    
    printf("Creating MiniVSFS image: %s\n", image_name);
    printf("Size: %lu KiB, Inodes: %lu\n", size_kib, inodes);
    

    uint64_t total_blocks = (size_kib * 1024) / BS;
    uint64_t inode_table_blocks = (inodes * INODE_SIZE + BS - 1) / BS;
    uint64_t data_region_start = 3 + inode_table_blocks;
    uint64_t data_region_blocks = total_blocks - data_region_start;
    

    superblock_t sb = {0};
    sb.magic = 0x4D565346;
    sb.version = 1;
    sb.block_size = BS;
    sb.total_blocks = total_blocks;
    sb.inode_count = inodes;
    sb.inode_bitmap_start = 1;
    sb.inode_bitmap_blocks = 1;
    sb.data_bitmap_start = 2;
    sb.data_bitmap_blocks = 1;
    sb.inode_table_start = 3;
    sb.inode_table_blocks = inode_table_blocks;
    sb.data_region_start = data_region_start;
    sb.data_region_blocks = data_region_blocks;
    sb.root_inode = 1;
    sb.mtime_epoch = time(NULL);
    sb.flags = 0;
    

    inode_t root_inode = {0};
    root_inode.mode = 0040000;
    root_inode.links = 2;
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.size_bytes = BS;
    root_inode.atime = sb.mtime_epoch;
    root_inode.mtime = sb.mtime_epoch;
    root_inode.ctime = sb.mtime_epoch;
    root_inode.direct[0] = data_region_start;
    for (int i = 1; i < 12; i++) {
        root_inode.direct[i] = 0;
    }
    root_inode.reserved_0 = 0;
    root_inode.reserved_1 = 0;
    root_inode.reserved_2 = 0;
    root_inode.proj_id = 3;
    root_inode.uid16_gid16 = 0;
    root_inode.xattr_ptr = 0;
    

    dirent64_t dot_entry = {0};
    dot_entry.inode_no = 1;
    dot_entry.type = 2; 
    strcpy(dot_entry.name, ".");
    
    dirent64_t dotdot_entry = {0};
    dotdot_entry.inode_no = 1;
    dotdot_entry.type = 2; 
    strcpy(dotdot_entry.name, "..");
    

    superblock_crc_finalize(&sb);
    inode_crc_finalize(&root_inode);
    dirent_checksum_finalize(&dot_entry);
    dirent_checksum_finalize(&dotdot_entry);
    

    FILE* fp = fopen(image_name, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot create image file %s\n", image_name);
        return 1;
    }
    

    uint8_t block_buffer[BS] = {0};
    memcpy(block_buffer, &sb, sizeof(superblock_t));
    fwrite(block_buffer, 1, BS, fp);
    

    memset(block_buffer, 0, BS);
    block_buffer[0] = 0x01;
    fwrite(block_buffer, 1, BS, fp);
    

    memset(block_buffer, 0, BS);
    block_buffer[0] = 0x01;
    fwrite(block_buffer, 1, BS, fp);
    

    for (uint64_t i = 0; i < inode_table_blocks; i++) {
        memset(block_buffer, 0, BS);
        if (i == 0) {

            memcpy(block_buffer, &root_inode, sizeof(inode_t));
        }
        fwrite(block_buffer, 1, BS, fp);
    }
    

    for (uint64_t i = 0; i < data_region_blocks; i++) {
        memset(block_buffer, 0, BS);
        if (i == 0) {

            memcpy(block_buffer, &dot_entry, sizeof(dirent64_t));
            memcpy(block_buffer + sizeof(dirent64_t), &dotdot_entry, sizeof(dirent64_t));
        }
        fwrite(block_buffer, 1, BS, fp);
    }
    
    fclose(fp);
    
    printf("Filesystem created successfully!\n");
    return 0;
}
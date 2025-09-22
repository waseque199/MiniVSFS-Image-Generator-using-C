#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12
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
    ino->inode_crc = (uint64_t)c;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];
    de->checksum = x;
}

int find_free_inode(uint8_t* inode_bitmap, uint64_t max_inodes) {
    for (uint64_t byte_idx = 0; byte_idx < (max_inodes + 7) / 8; byte_idx++) {
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            uint64_t inode_num = byte_idx * 8 + bit_idx + 1;
            if (inode_num > max_inodes) return -1;
            
            if (!(inode_bitmap[byte_idx] & (1 << bit_idx))) {
            
                inode_bitmap[byte_idx] |= (1 << bit_idx);
                return inode_num;
            }
        }
    }
    return -1;
}

int find_free_data_block(uint8_t* data_bitmap, uint64_t max_blocks) {
    for (uint64_t byte_idx = 0; byte_idx < (max_blocks + 7) / 8; byte_idx++) {
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            uint64_t block_num = byte_idx * 8 + bit_idx;
            if (block_num >= max_blocks) return -1;
            
            if (!(data_bitmap[byte_idx] & (1 << bit_idx))) {
            
                data_bitmap[byte_idx] |= (1 << bit_idx);
                return block_num;
            }
        }
    }
    return -1;
}

int find_free_dirent(dirent64_t* dirents, int max_entries) {
    for (int i = 0; i < max_entries; i++) {
        if (dirents[i].inode_no == 0) {
            return i;
        }
    }
    return -1;
}

void print_usage(const char* prog_name) {
    printf("Usage: %s --input <input.img> --output <output.img> --file <filename>\n", prog_name);
}

int parse_args(int argc, char* argv[], char** input_name, char** output_name, char** file_name) {
    if (argc != 7) {
        return -1;
    }
    
    *input_name = NULL;
    *output_name = NULL;
    *file_name = NULL;
    
    for (int i = 1; i < argc; i += 2) {
        if (strcmp(argv[i], "--input") == 0) {
            *input_name = argv[i + 1];
        } else if (strcmp(argv[i], "--output") == 0) {
            *output_name = argv[i + 1];
        } else if (strcmp(argv[i], "--file") == 0) {
            *file_name = argv[i + 1];
        } else {
            return -1;
        }
    }
    
    if (*input_name == NULL || *output_name == NULL || *file_name == NULL) {
        return -1;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    crc32_init();
    

    char* input_name;
    char* output_name;
    char* file_name;
    
    if (parse_args(argc, argv, &input_name, &output_name, &file_name) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    
    printf("Adding file '%s' to filesystem\n", file_name);
    printf("Input: %s, Output: %s\n", input_name, output_name);
    

    struct stat input_stat;
    if (stat(file_name, &input_stat) != 0) {
        fprintf(stderr, "Error: File %s not found\n", file_name);
        return 1;
    }
    
    if (input_stat.st_size > 12 * BS) {
        fprintf(stderr, "Error: File too large (max 12 blocks = %d bytes)\n", 12 * BS);
        return 1;
    }
    

    FILE* input_fp = fopen(input_name, "rb");
    if (!input_fp) {
        fprintf(stderr, "Error: Cannot open input image %s\n", input_name);
        return 1;
    }
    

    superblock_t sb;
    if (fread(&sb, sizeof(superblock_t), 1, input_fp) != 1) {
        fprintf(stderr, "Error: Cannot read superblock\n");
        fclose(input_fp);
        return 1;
    }
    

    if (sb.magic != 0x4D565346) {
        fprintf(stderr, "Error: Invalid filesystem magic number\n");
        fclose(input_fp);
        return 1;
    }
    

    fseek(input_fp, 0, SEEK_END);
    long fs_size = ftell(input_fp);
    fseek(input_fp, 0, SEEK_SET);
    
    uint8_t* fs_data = malloc(fs_size);
    if (!fs_data) {
        fprintf(stderr, "Error: Cannot allocate memory\n");
        fclose(input_fp);
        return 1;
    }
    
    if (fread(fs_data, 1, fs_size, input_fp) != (size_t)fs_size) {
        fprintf(stderr, "Error: Cannot read filesystem data\n");
        fclose(input_fp);
        free(fs_data);
        return 1;
    }
    fclose(input_fp);
    

    uint8_t* inode_bitmap = fs_data + sb.inode_bitmap_start * BS;
    uint8_t* data_bitmap = fs_data + sb.data_bitmap_start * BS;
    inode_t* inode_table = (inode_t*)(fs_data + sb.inode_table_start * BS);
    uint8_t* data_region = fs_data + sb.data_region_start * BS;
    

    int new_inode_num = find_free_inode(inode_bitmap, sb.inode_count);
    if (new_inode_num == -1) {
        fprintf(stderr, "Error: No free inodes available\n");
        free(fs_data);
        return 1;
    }
    

    int blocks_needed = (input_stat.st_size + BS - 1) / BS;
    if (blocks_needed > 12) {
        fprintf(stderr, "Error: File too large for direct blocks only\n");
        free(fs_data);
        return 1;
    }
    

    uint32_t data_blocks[12] = {0};
    for (int i = 0; i < blocks_needed; i++) {
        int block_idx = find_free_data_block(data_bitmap, sb.data_region_blocks);
        if (block_idx == -1) {
            fprintf(stderr, "Error: No free data blocks available\n");
            free(fs_data);
            return 1;
        }
        data_blocks[i] = sb.data_region_start + block_idx;
    }
    
    // Create new inode for the file
    inode_t* new_inode = &inode_table[new_inode_num - 1]; 
    memset(new_inode, 0, sizeof(inode_t));
    new_inode->mode = 0100000; 
    new_inode->links = 1;
    new_inode->uid = 0;
    new_inode->gid = 0;
    new_inode->size_bytes = input_stat.st_size;
    new_inode->atime = time(NULL);
    new_inode->mtime = time(NULL);
    new_inode->ctime = time(NULL);
    
    for (int i = 0; i < blocks_needed; i++) {
        new_inode->direct[i] = data_blocks[i];
    }
    for (int i = blocks_needed; i < 12; i++) {
        new_inode->direct[i] = 0;
    }
    
    new_inode->reserved_0 = 0;
    new_inode->reserved_1 = 0;
    new_inode->reserved_2 = 0;
    new_inode->proj_id = 3;
    new_inode->uid16_gid16 = 0;
    new_inode->xattr_ptr = 0;
    

    FILE* file_fp = fopen(file_name, "rb");
    if (!file_fp) {
        fprintf(stderr, "Error: Cannot open file %s\n", file_name);
        free(fs_data);
        return 1;
    }
    
    for (int i = 0; i < blocks_needed; i++) {
        uint8_t block_data[BS] = {0};
        size_t bytes_to_read = (i == blocks_needed - 1) ? 
            (input_stat.st_size - i * BS) : BS;
        if (fread(block_data, 1, bytes_to_read, file_fp) != bytes_to_read) {
            fprintf(stderr, "Error: Cannot read file data\n");
            fclose(file_fp);
            free(fs_data);
            return 1;
        }
        
        uint64_t block_offset = (data_blocks[i] - sb.data_region_start) * BS;
        memcpy(data_region + block_offset, block_data, BS);
    }
    fclose(file_fp);
    

    inode_t* root_inode = &inode_table[0]; 
    uint64_t root_data_block_offset = (root_inode->direct[0] - sb.data_region_start) * BS;
    dirent64_t* root_dirents = (dirent64_t*)(data_region + root_data_block_offset);
    
    int max_dirents = BS / sizeof(dirent64_t);
    int free_dirent_idx = find_free_dirent(root_dirents, max_dirents);
    if (free_dirent_idx == -1) {
        fprintf(stderr, "Error: No free directory entries in root\n");
        free(fs_data);
        return 1;
    }
    
  
    dirent64_t* new_dirent = &root_dirents[free_dirent_idx];
    memset(new_dirent, 0, sizeof(dirent64_t));
    new_dirent->inode_no = new_inode_num;
    new_dirent->type = 1; // file
    strncpy(new_dirent->name, file_name, 57);
    new_dirent->name[57] = '\0'; 
    

    root_inode->links++;
    

    inode_crc_finalize(new_inode);
    inode_crc_finalize(root_inode);
    dirent_checksum_finalize(new_dirent);
    

    superblock_t* sb_ptr = (superblock_t*)fs_data;
    superblock_crc_finalize(sb_ptr);
    

    FILE* output_fp = fopen(output_name, "wb");
    if (!output_fp) {
        fprintf(stderr, "Error: Cannot create output file %s\n", output_name);
        free(fs_data);
        return 1;
    }
    
    fwrite(fs_data, 1, fs_size, output_fp);
    fclose(output_fp);
    
    free(fs_data);
    
    printf("File added successfully!\n");
    return 0;
}
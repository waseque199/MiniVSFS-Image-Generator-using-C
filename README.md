# MiniVSFS Project Implementation

This project implements a simplified file system called MiniVSFS (Mini Very Simple File System) with two main utilities:

1. **mkfs_builder** - Creates a raw disk image with the MiniVSFS file system structure
2. **mkfs_adder** - Adds files to an existing MiniVSFS image

## Project Structure

### Key Features Implemented

- **Superblock**: Contains filesystem metadata including magic number, block layout, and checksums
- **Inode Bitmap**: Tracks allocated/free inodes (1 bit per inode)
- **Data Bitmap**: Tracks allocated/free data blocks (1 bit per block) 
- **Inode Table**: Stores inode structures with file metadata
- **Data Region**: Contains actual file data and directory entries
- **Root Directory**: Pre-created with "." and ".." entries
- **CRC32 Checksums**: Data integrity verification for all structures

### File System Layout

```
| Superblock | Inode Bitmap | Data Bitmap | Inode Table | Data Region |
|  (1 block) |   (1 block)  |  (1 block)  |  (N blocks) | (M blocks)  |
```

## Building the Project

### Compilation

```bash
# Build mkfs_builder
gcc -O2 -std=c17 -Wall -Wextra mkfs_builder.c -o mkfs_builder

# Build mkfs_adder  
gcc -O2 -std=c17 -Wall -Wextra mkfs_adder.c -o mkfs_adder
```

### Usage

#### Creating a File System

```bash
./mkfs_builder --image filesystem.img --size-kib 180 --inodes 128
```

**Parameters:**
- `--image`: Output image filename
- `--size-kib`: Total filesystem size in KiB (180-4096, multiple of 4)
- `--inodes`: Number of inodes (128-512)

#### Adding Files to File System

```bash
./mkfs_adder --input filesystem.img --output updated.img --file myfile.txt
```

**Parameters:**
- `--input`: Input filesystem image
- `--output`: Output filesystem image (with added file)
- `--file`: File to add to the filesystem

## Implementation Details

### Data Structures

#### Superblock (116 bytes)
- Magic number: `0x4D565346` ("MVSF")
- Version: 1
- Block size: 4096 bytes
- Layout information for all filesystem components
- CRC32 checksum for integrity

#### Inode (128 bytes)  
- File type and permissions (mode)
- Link count, user/group IDs
- File size, timestamps (atime, mtime, ctime)
- 12 direct block pointers
- CRC32 checksum

#### Directory Entry (64 bytes)
- Inode number
- File type (1=file, 2=directory)
- Filename (58 characters max)
- XOR checksum

### Key Algorithms

#### Bitmap Management
- **First-fit allocation**: Finds first available inode/block
- **Bit manipulation**: Sets/clears bits for allocation tracking

#### File Allocation
- **Direct blocks only**: Maximum 12 blocks per file (48KB max)
- **Block alignment**: Files padded to block boundaries
- **Contiguous storage**: Each file's blocks stored separately

#### Directory Management  
- **Root directory only**: Single flat directory structure
- **Fixed entries**: "." and ".." always present
- **Linear search**: Finds free directory entry slots

### Error Handling

The implementation includes comprehensive error handling for:
- Invalid command line parameters
- File I/O errors
- Filesystem corruption detection
- Resource exhaustion (no free inodes/blocks)
- File size limitations

## Testing

### Basic Test Sequence

```bash
# Create filesystem
./mkfs_builder --image test.img --size-kib 180 --inodes 128

# Add a file
echo "Hello World" > testfile.txt
./mkfs_adder --input test.img --output test2.img --file testfile.txt

# Verify with hexdump
hexdump -C test2.img | head -20
```

### Validation Steps

1. **Superblock validation**: Check magic number and layout
2. **Bitmap consistency**: Verify allocated bits match used resources  
3. **Inode integrity**: Validate CRC checksums
4. **Directory structure**: Confirm root directory entries
5. **File data**: Verify file contents stored correctly

### Sample Files Testing

The project includes sample files for testing:
- `file_15.txt`: "falcon yankee narwhal rhinoceros xylophone delta quokka walrus juliet mike"
- `file_22.txt`: "walrus uniform salamander zulu iguana apple umbrella echo iguana november"
- `file_26.txt`: "tiger romeo banana iguana yankee mike penguin dragonfruit juliet xylophone"
- `file_9.txt`: "umbrella tango quokka banana quokka yankee falcon yak salamander umbrella"

## Technical Specifications

### Constraints
- **Block size**: 4096 bytes (fixed)
- **Inode size**: 128 bytes (fixed)  
- **Maximum file size**: 48KB (12 direct blocks)
- **Directory limit**: One root directory only
- **Filename length**: 57 characters maximum
- **Endianness**: Little-endian format

### Memory Layout
- All structures use `#pragma pack(push, 1)` for exact binary layout
- Static assertions verify structure sizes
- Proper alignment for cross-platform compatibility

### Security Features
- **Checksum validation**: CRC32 for superblock and inodes
- **XOR checksums**: For directory entries
- **Magic number verification**: Prevents operation on invalid images
- **Bounds checking**: Prevents buffer overflows

## Debugging Tips

### Using hexdump/xxd
```bash
# View filesystem structure
xxd -l 512 filesystem.img

# Check specific blocks
xxd -s +4096 -l 256 filesystem.img  # View second block
```

### Common Issues
- **Structure alignment**: Ensure `#pragma pack` is used correctly
- **Endianness**: All multi-byte values stored little-endian
- **Checksum calculation**: Must zero checksum fields before computing
- **Bitmap indexing**: Remember inodes are 1-indexed, blocks are 0-indexed

## Future Enhancements

Potential improvements for a production system:
- Indirect block pointers for larger files
- Subdirectory support
- Extended attributes
- Journaling for crash recovery
- Compression support
- Access control lists (ACLs)

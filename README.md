# OS Filesystem
Simple File System Abstraction

## Overall architecture of sectors:
Some empty spaces exist on the first parts of the hard sectors for convenience in addressing.

Only 1000 inodes exist because of limitation on `MAX_FILES`, thus the magic number and inode bitmap can fit into the first sector.

There are 10000 sectors available for the hard drive, so 10000/8 bytes =1250 bytes ~ 3 sectors are needed for storing the datablocks bit map. Some bytes of the last sector (sector 3) are left unused.

Since each inode is exactly 128 bytes, 4 inodes can be stored in a single sector and 1000 inodes can exist at most. So 250 sectors are needed, again for the ease of convenice in addressing using the datablock bitmaps we ignore sectors 254 and 255 and the real datablocks start from the sector 256. The overall overhead of the metadata is 2.56% which is comparable to filesystems like ext4, and a little more because we store many (30) pointers for pointing to data blocks and no indrect addressing mode is available.

```
#-----Sector 0-----#
|    Magic Number  |
|  iNode   Bitmap  |
|-----Sector 1-----|
| Datablock Bitmap |
|        .         |
|        .         |
|-----Sector 4-----|
|  iNodes Blocks   |
|        .         |
|        .         |
|        .         |
|----Sector 254----|
|                  |
|    Empty Space!  |
|                  |
|----Sector 256----|
|                  |
|                  |
|    Data Blocks   |
|                  |
|                  |
#---End of Disk----#
```

## Structures:
### `struct file_descriptor`:
`int inode_number`: points to the inode number of the file

`int pointer`: stores the position of the pointer

There is an array of type `file_descriptor` with size `MAX_FDS` which stores all open file descriptors in ram. Whenever a new file descriptor is needed using the `last_fd` variable we loop through the array to find the next empty position for a file descriptor and assign it. `last_fd` is used to increase search speed, assuming there is time locality and when the last descriptors are assigned, the first ones are free.

### `int inode_open_count[MAX_FILES]`:
This array stores how many file descriptors are currently open for each inode. Since inodes are at most `MAX_FILES`, the size of this array should be the same.

### `struct inode`:
`int size`: Stores how much is the file size. For directories this is the same as `number_of_records * 20` bytes.

`int type`: 0 for directory and 1 for file. `int` is used instead of `char`, in order to make the inode size exactly 128 bytes.

`SECTOR_NUM data_blocks[DATA_BLOCK_PER_INODE]`: an array of 30 integers pointing to data blocks of the inode.

### `struct file_record`:
`char name[16]`: a null terminated string with size of at most 16 bytes.

`int inode_number`: inode number of the file reffering to

This structure is used inside data blocks of directories, in order to store the name of files and subdirectories. The size is exactly 20 bytes.

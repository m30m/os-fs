#include <string.h>
#include <assert.h>
#include "LibFS.h"
#include "LibDisk.h"

//TODO: Handle trailing /

#define MAX_FILES  1000
#define MAX_FDS 1000
#define DATA_BLOCK_PER_INODE 30

const int MAGIC_NUMBER = 241543903;
const int INODE_BITMAP_SIZE = 125; // MAX_FILES / BITS_IN_A_SINGLE_BYTE(8)
const int MAGIC_NUMBER_SIZE = 4;

typedef int SECTOR_NUM;


// global errno value here
int osErrno;

enum INODE_TYPE
{
    DIR_TYPE, FILE_TYPE
};

struct inode
{
    int size;
    int type;
    SECTOR_NUM data_blocks[DATA_BLOCK_PER_INODE];
};

struct file_record
{
    char name[16];
    int inode_number;
};

void read_from_single_sector(int sector, int offset, void *buffer, size_t size)
{
    /*
     * Util function to read only a part of sector
     */
    char *tmp = malloc(SECTOR_SIZE);
    Disk_Read(sector, tmp);
    memcpy(buffer, &tmp[offset], size);
    free(tmp);
}

void write_to_single_sector(int sector, int offset, void *buffer, size_t size)
{
    /*
     * Util function to write only a part of sector
     * First the sector is read completely, then the changes are applied and then written back to hard
     */
    char *tmp = malloc(SECTOR_SIZE);
    Disk_Read(sector, tmp);
    memcpy(&tmp[offset], buffer, size);
    Disk_Write(sector, tmp);
    free(tmp);
}

void set_inode_bitmap(int inode_number, char value)
{
    int byte_position = MAGIC_NUMBER_SIZE + inode_number / 8;
    int byte_offset = inode_number % 8;
    char c;
    read_from_single_sector(0, byte_position, &c, 1);
    if (value == 1)
        c |= 1 << byte_offset;
    else
        c &= ~(1 << byte_offset);
    write_to_single_sector(0, byte_position, &c, 1);
}

char get_inode_bitmap(int inode_number)
{
    int byte_position = MAGIC_NUMBER_SIZE + inode_number / 8;
    int byte_offset = inode_number % 8;
    char c;
    read_from_single_sector(0, byte_position, &c, 1);
    return (char) ((c >> byte_offset) & 1);
}

void set_datablock_bitmap(int block_number, char value)
{
    int sector = 1 + block_number / (SECTOR_SIZE * 8);
    int byte_position = (block_number % (SECTOR_SIZE * 8)) / 8;
    int byte_offset = (block_number % (SECTOR_SIZE * 8)) % 8;
    char c;
    read_from_single_sector(sector, byte_position, &c, 1);
    if (value == 1)
        c |= 1 << byte_offset;
    else
        c &= ~(1 << byte_offset);
    write_to_single_sector(sector, byte_position, &c, 1);
}

char get_datablock_bitmap(int block_number)
{
    int sector = 1 + block_number / (SECTOR_SIZE * 8);
    int byte_position = (block_number % (SECTOR_SIZE * 8)) / 8;
    int byte_offset = (block_number % (SECTOR_SIZE * 8)) % 8;
    char c;
    read_from_single_sector(sector, byte_position, &c, 1);
    return (char) ((c >> byte_offset) & 1);
}

int inode_number_to_sector_number(int inode_number)
{ return 1 + 3 + inode_number / 4; }

int inode_number_to_sector_offset(int inode_number)
{ return (int) ((inode_number % 4) * sizeof(struct inode)); }

void read_inode(int inode_number, struct inode *node)
{
    /*
     * Reads the inode with number `inode_number` from hard and stores it in `node`
     */
    read_from_single_sector(inode_number_to_sector_number(inode_number), inode_number_to_sector_offset(inode_number),
                            node,
                            sizeof(struct inode));
}

void write_inode(int inode_number, struct inode *node)
{
    /*
     * Write the inode with number `inode_number` to hard using `node` variable
     */
    write_to_single_sector(inode_number_to_sector_number(inode_number), inode_number_to_sector_offset(inode_number),
                           node,
                           sizeof(struct inode));
}

int get_new_block()
{
    /*
     * Searches through the bitmap and assign the first empty block number
     */
    char *tmp = malloc(SECTOR_SIZE);
    int i;
    for (i = 1; i < 4; i++)
    {
        Disk_Read(i, tmp);
        int j = 0;
        if (i == 1)
            j = 254 / 8 + 1; // First 254 sectors are metadata
        for (; j < SECTOR_SIZE; j++)
        {
            int k;
            for (k = 0; k < 8; k++)
            {
                if ((tmp[j] & (1 << k)) == 0)
                {
                    tmp[j] |= (1 << k);
                    Disk_Write(i, tmp);
                    int sector_number = (i - 1) * SECTOR_SIZE * 8 + j * 8 + k;
                    memset(tmp, 0, SECTOR_SIZE);
                    Disk_Write(sector_number, tmp);
                    free(tmp);
                    return sector_number;
                }
            }
        }
    }
    free(tmp);
    return -1;//No free blocks
}

int get_new_inode(struct inode **new_node)
{
    /*
     * Searches through the bitmap and assign the first empty inode number
     */
    //TODO:Optimize this by reading the sector once
    int i;
    for (i = 0; i < MAX_FILES; i++)
    {
        if (!get_inode_bitmap(i))
        {
            set_inode_bitmap(i, 1);
            (*new_node) = calloc(1, sizeof(struct inode));
            write_inode(i, *new_node);
            return i;
        }
    }
    return -1;
}

int find_last_parent(char *file, struct inode **new_node)
{
    /*
     * Finds the inode of the last parent, returns the inode number and store the inode in `new_node` variable
     * /path/path2/path3/file
     *              ^
     *        inode returned
     */
    struct inode *parent = calloc(1, sizeof(struct inode));
    int parent_inode_number = 0;
    read_inode(parent_inode_number, parent);
    if (strcmp(file, "/") == 0)
    {
        (*new_node) = parent;
        return parent_inode_number;
    }
    int start_pos = 1; //Ignoring the first slash
    int end_pos;
    char *tmp = malloc(SECTOR_SIZE);
    struct file_record *tmp_file_record = malloc(sizeof(struct file_record));
    while (1)
    {
        end_pos = start_pos;
        while (file[end_pos] != '\0' && file[end_pos] != '/')
            end_pos++;
        if (file[end_pos] == '\0')
        {
            (*new_node) = parent;
            free(tmp);
            free(tmp_file_record);
            return parent_inode_number;
        }
        if (start_pos == end_pos)
        {
            fprintf(stderr, "Wrong format of file path\n");
            osErrno = E_CREATE;
            free(parent);
            free(tmp);
            free(tmp_file_record);
            return -1;
        }
        if (end_pos - start_pos >= 16)
        {
            fprintf(stderr, "File is longer than 16 character\n");
            osErrno = E_CREATE;
            free(parent);
            free(tmp);
            free(tmp_file_record);
            return -1;
        }
        char tmp_path[16];
        strncpy(tmp_path, &file[start_pos], end_pos - start_pos);
        tmp_path[end_pos - start_pos] = '\0';
        start_pos = end_pos + 1;

        //folder
        {
            int i;
            char found_folder = 0;
            for (i = 0; i < DATA_BLOCK_PER_INODE && !found_folder; i++)
            {
                if (parent->data_blocks[i] != 0)
                {
                    Disk_Read(parent->data_blocks[i], tmp);
                    int j;
                    for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
                    {
                        memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                        if (strcmp(tmp_file_record->name, tmp_path) == 0)
                        {
                            parent_inode_number = tmp_file_record->inode_number;
                            read_inode(parent_inode_number, parent);
                            if (parent->type != DIR_TYPE)
                            {
                                fprintf(stderr, "Expected folder but found file: %s\n", tmp_path);
                                free(parent);
                                free(tmp);
                                free(tmp_file_record);
                                return -1;
                            }
                            found_folder = 1;
                            break;

                        }
                    }
                }
            }
            if (!found_folder)
            {
                fprintf(stderr, "Folder %s not found\n", tmp_path);
                free(parent);
                free(tmp);
                free(tmp_file_record);
                return -1;
            }
        }
    }
}

int
file_folder_create(char *file, enum INODE_TYPE type)
{
    printf("FS_Create\n");
    struct inode *parent;
    int parent_inode_number = 0;
    parent_inode_number = find_last_parent(file, &parent);
    if (parent_inode_number == -1)
        return -1;

    int end_pos = (int) strlen(file);
    int start_pos = end_pos - 1; //Ignoring the first slash
    char *tmp = malloc(SECTOR_SIZE);
    struct file_record *tmp_file_record = malloc(sizeof(struct file_record));

    while (file[start_pos] != '/')
        start_pos--;
    start_pos++;
    if (start_pos == end_pos)
    {
        fprintf(stderr, "Create directory with Create_dir command\n");
        osErrno = E_CREATE;
        return -1;
    }
    if (end_pos - start_pos >= 16)
    {
        fprintf(stderr, "File is longer than 16 character\n");
        osErrno = E_CREATE;
        return -1;
    }
    char tmp_path[16];
    strncpy(tmp_path, &file[start_pos], end_pos - start_pos);
    tmp_path[end_pos - start_pos] = '\0';
    int i;
    for (i = 0; i < DATA_BLOCK_PER_INODE; i++)
    {
        if (parent->data_blocks[i] != 0)
        {
            Disk_Read(parent->data_blocks[i], tmp);
            int j;
            for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
            {
                memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                if (strcmp(tmp_file_record->name, tmp_path) == 0)
                {
                    fprintf(stderr, "File already exists\n");
                    osErrno = E_CREATE;
                    return -1;
                }
            }
        }
    }
    for (i = 0; i < DATA_BLOCK_PER_INODE; i++)
    {
        if (parent->data_blocks[i] == 0)
        {
            int block = get_new_block();
            if (block == -1)//no block available but there is still some chance
                continue;
            parent->data_blocks[i] = block;
        }
        Disk_Read(parent->data_blocks[i], tmp);
        int j;
        for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
        {
            memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
            if (tmp_file_record->inode_number == 0)//hooray! found free record
            {
                struct inode *new_node;
                int new_inode_number = get_new_inode(&new_node);
                if (new_inode_number == -1)
                {
                    fprintf(stderr, "No free inode available\n");
                    osErrno = E_CREATE;
                    free(tmp);
                    free(tmp_file_record);
                    free(new_node);
                    return -1;
                }
                new_node->type = type;
                write_inode(new_inode_number, new_node);
                tmp_file_record->inode_number = new_inode_number;
                strcpy(tmp_file_record->name, tmp_path);
                memcpy(&tmp[j * sizeof(struct file_record)], tmp_file_record, sizeof(struct file_record));
                Disk_Write(parent->data_blocks[i], tmp);
                write_inode(parent_inode_number, parent);
                free(tmp);
                free(tmp_file_record);
                free(new_node);
                return 0;
            }
        }
    }
    //follow the path, if not exists error
    // append the file to the parent directory
    free(parent);
    return 0;
}

int last_fd;
int open_file_count = 0;
struct file_descriptor
{
    int inode_number;
    int pointer;
};
struct file_descriptor file_descriptors[MAX_FDS];

int get_new_fd()
{
    /*
     * Returns the first empty file descriptor, performance is increased using the `last_fd` variable
     */
    while (file_descriptors[last_fd].inode_number != 0)
    {
        last_fd = (last_fd + 1) % MAX_FDS;
    }
    return last_fd;
}

int inode_open_count[MAX_FILES];

char *image_path = NULL; //Used for `FS_Sync`

void initialize_filesystem(char *path, int *magic_number)
{
    write_to_single_sector(0, 0, magic_number, 4);
    struct inode *root = calloc(1, sizeof(struct inode));
    root->size = 0;
    root->type = DIR_TYPE;
    set_inode_bitmap(0, 1);
    free(root);
    Disk_Save(path);
}

int find_inode(char *file, struct inode **node)
{
    /*
     * Finds the inode of file, returns the inode number and store the inode in `node` variable
     * /path/path2/path3/file
     *                    ^
     *              inode returned
     */
    struct inode *parent;
    int parent_inode_number = 0;
    parent_inode_number = find_last_parent(file, &parent);
    if (parent_inode_number == -1)
    {
        fprintf(stderr, "Folder does not exists\n");
        return -1;
    }

    int end_pos = (int) strlen(file);
    int start_pos = end_pos - 1; //Ignoring the first slash

    while (file[start_pos] != '/')
        start_pos--;
    start_pos++;
    if (end_pos - start_pos >= 16)
    {
        fprintf(stderr, "File is longer than 16 character\n");
        free(parent);
        return -1;
    }
    char tmp_path[16];
    strncpy(tmp_path, &file[start_pos], end_pos - start_pos);
    tmp_path[end_pos - start_pos] = '\0';
    int i;
    char *tmp = malloc(SECTOR_SIZE);
    struct file_record *tmp_file_record = malloc(sizeof(struct file_record));
    for (i = 0; i < DATA_BLOCK_PER_INODE; i++)
    {
        if (parent->data_blocks[i] != 0)
        {
            Disk_Read(parent->data_blocks[i], tmp);
            int j;
            for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
            {
                memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                if (strcmp(tmp_file_record->name, tmp_path) == 0)
                {
                    struct inode *new_node = calloc(1, sizeof(struct inode));
                    read_inode(tmp_file_record->inode_number, new_node);
                    (*node) = new_node;
                    free(parent);
                    free(tmp);
                    free(tmp_file_record);
                    return tmp_file_record->inode_number;
                }
            }
        }
    }
    free(parent);
    free(tmp);
    free(tmp_file_record);
    return -1;
}

int
FS_Boot(char *path)
{
    printf("FS_Boot %s\n", path);

    // oops, check for errors
    if (Disk_Init() == -1)
    {
        printf("Disk_Init() failed\n");
        osErrno = E_GENERAL;
        return -1;
    }

    int magic_number = MAGIC_NUMBER;
    if (Disk_Load(path) == -1)
    {
        if (diskErrno == E_OPENING_FILE)
        {
            initialize_filesystem(path, &magic_number);
            fprintf(stderr, "Filesystem didn't exist, creating new file\n");
        } else
        {
            osErrno = E_GENERAL;
            return -1;
        }
    } else
    {
        int tmp;
        read_from_single_sector(0, 0, &tmp, 4);
        if (tmp != magic_number)
        {
            osErrno = E_GENERAL;
            fprintf(stderr, "Magic number didn't match\n");
            return -1;
        }
    }

    image_path = path;
    open_file_count = 0;
    last_fd = 0;
    memset(file_descriptors, 0, sizeof file_descriptors);
    memset(inode_open_count, 0, sizeof inode_open_count);
    return 0;
}

int
FS_Sync()
{
    printf("FS_Sync\n");
    if (Disk_Save(image_path) == -1)
    {
        osErrno = E_GENERAL;
        return -1;
    }
    return 0;
}

int
File_Create(char *file)
{
    return file_folder_create(file, FILE_TYPE);
}

int
File_Open(char *file)
{
    printf("FS_Open\n");
    if (open_file_count == MAX_FDS)
    {
        fprintf(stderr, "Too many open files\n");
        osErrno = E_TOO_MANY_OPEN_FILES;
        return -1;
    }
    int inode_number;
    struct inode *node;
    inode_number = find_inode(file, &node);
    if (inode_number == -1)
    {
        fprintf(stderr, "No such file to open\n");
        osErrno = E_NO_SUCH_FILE;
        return -1;
    }
    if (node->type == DIR_TYPE)
    {
        fprintf(stderr, "Can't open dir\n");
        osErrno = E_NO_SUCH_FILE;
        free(node);
        return -1;
    }
    int fd = get_new_fd();
    file_descriptors[fd].inode_number = inode_number;
    file_descriptors[fd].pointer = 0;
    open_file_count++;
    inode_open_count[inode_number]++;
    return fd;
}

int
File_Read(int fd_num, void *buffer, int size)
{
    printf("FS_Read\n");
    struct file_descriptor *fd = &file_descriptors[fd_num];
    if (fd->inode_number == 0)
    {
        osErrno = E_BAD_FD;
        return -1;
    }
    struct inode *node = calloc(1, sizeof(struct inode));
    read_inode(fd->inode_number, node);

    int block_number = fd->pointer / SECTOR_SIZE;
    int block_offset = fd->pointer % SECTOR_SIZE;
    int actual_size = size;
    if (fd->pointer + size > node->size)
        actual_size = node->size - fd->pointer;

    int read_left = actual_size;
    int read_done = 0;
    while (read_left > 0)
    {
        int read_amount = read_left;
        if (read_amount > SECTOR_SIZE - block_offset)
            read_amount = SECTOR_SIZE - block_offset;
        read_from_single_sector(node->data_blocks[block_number], block_offset, &buffer[read_done], read_amount);
        block_number++;
        block_offset = 0;
        read_left -= read_amount;
        read_done += read_amount;
    }
    fd->pointer += actual_size;
    free(node);
    return read_done;
}

int
File_Write(int fd_num, void *buffer, int size)
{
    printf("FS_Write\n");
    struct file_descriptor *fd = &file_descriptors[fd_num];
    if (fd->inode_number == 0)
    {
        osErrno = E_BAD_FD;
        return -1;
    }
    struct inode *node = calloc(1, sizeof(struct inode));
    read_inode(fd->inode_number, node);

    int block_number = fd->pointer / SECTOR_SIZE;
    int block_offset = fd->pointer % SECTOR_SIZE;

    int write_left = size;
    int write_done = 0;
    while (write_left > 0)
    {
        if (block_number == DATA_BLOCK_PER_INODE)
        {
            fprintf(stderr, "No more blocks left in inode, file is too big!\n");
            osErrno = E_FILE_TOO_BIG;
            free(node);
            return -1;
        }
        if (node->data_blocks[block_number] == 0)//allocate new block
        {
            int new_sector_number = get_new_block();
            if (new_sector_number == -1)
            {
                fprintf(stderr, "No space left on device for more writing\n");
                osErrno = E_NO_SPACE;
                free(node);
                return -1;
            }
            node->data_blocks[block_number] = new_sector_number;
        }
        int write_amount = write_left;
        if (write_left > SECTOR_SIZE - block_offset)
            write_amount = SECTOR_SIZE - block_offset;
        write_to_single_sector(node->data_blocks[block_number], block_offset, &buffer[write_done], write_amount);
        write_done += write_amount;
        write_left -= write_amount;
        block_number++;
        block_offset = 0;
        if (node->size < fd->pointer + write_done)
        {
            node->size = fd->pointer + write_done;
            write_inode(fd->inode_number, node);
        }
    }
    fd->pointer += write_done;
    free(node);
    return 0;
}

int
File_Seek(int fd_num, int offset)
{
    printf("FS_Seek\n");
    struct file_descriptor *fd = &file_descriptors[fd_num];
    if (fd->inode_number == 0)
    {
        osErrno = E_BAD_FD;
        return -1;
    }
    struct inode *node = calloc(1, sizeof(struct inode));
    read_inode(fd->inode_number, node);
    if (offset < 0 || offset > node->size)
    {
        fprintf(stderr, "Seek position out of bound\n");
        osErrno = E_SEEK_OUT_OF_BOUNDS;
        free(node);
        return -1;
    }
    fd->pointer = offset;
    free(node);
    return fd->pointer;
}

int
File_Close(int fd)
{
    printf("FS_Close\n");
    if (file_descriptors[fd].inode_number == 0)
    {
        osErrno = E_BAD_FD;
        return -1;
    }
    inode_open_count[file_descriptors[fd].inode_number]--;
    open_file_count--;
    file_descriptors[fd].inode_number = 0;
    file_descriptors[fd].pointer = 0;
    return 0;
}

//Dir Ops

int
Dir_Create(char *path)
{
    printf("Dir_Create %s\n", path);
    //same as file but change the type to directory
    file_folder_create(path, DIR_TYPE);
    return 0;
}

int
Dir_Size(char *path)
{
    printf("Dir_Size\n");
    struct inode *node;
    find_inode(path, &node);
    int i;
    int entry_count = 0;
    char *tmp = malloc(SECTOR_SIZE);
    struct file_record *tmp_file_record = malloc(sizeof(struct file_record));

    if (node->type != DIR_TYPE)
    {
        fprintf(stderr, "Dir_Size should be used for directories\n");
        free(tmp);
        free(tmp_file_record);
        return -1;
    }

    for (i = 0; i < DATA_BLOCK_PER_INODE; i++)
    {
        if (node->data_blocks[i] != 0)
        {
            Disk_Read(node->data_blocks[i], tmp);
            int j;
            for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
            {
                memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                if (tmp_file_record->inode_number != 0)
                    entry_count++;
            }
        }
    }
    free(tmp);
    free(tmp_file_record);
    return (int) (entry_count * sizeof(struct file_record));
}

int
Dir_Read(char *path, void *buffer, int size)
{
    printf("Dir_Read\n");
    int inode_number;
    struct inode *node;
    inode_number = find_last_parent(path, &node);
    struct file_record *tmp_file_record = malloc(sizeof(struct file_record));
    char *tmp = malloc(SECTOR_SIZE);
    if (inode_number == -1)
    {
        free(node);
        free(tmp_file_record);
        return -1;
    }
    if (Dir_Size(path) > size)
    {
        osErrno = E_BUFFER_TOO_SMALL;
        free(node);
        free(tmp_file_record);
        return -1;
    }
    int i;
    int entry_count = 0;
    for (i = 0; i < DATA_BLOCK_PER_INODE; i++)
    {
        if (node->data_blocks[i] != 0)
        {
            Disk_Read(node->data_blocks[i], tmp);
            int j;
            for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
            {
                memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                if (tmp_file_record->inode_number == 0)continue;
                size -= (int) sizeof(struct file_record);
                memcpy(&buffer[entry_count * sizeof(struct file_record)], &tmp[j * sizeof(struct file_record)],
                       sizeof(struct file_record));
                entry_count++;
            }
        }
    }
    free(node);
    free(tmp_file_record);
    return entry_count;
}

int
Dir_Unlink(char *path)
{
    printf("Dir_Unlink\n");

    if (strcmp(path, "/") == 0)
    {
        osErrno = E_ROOT_DIR;
        return -1;
    }

    struct inode *parent;
    struct inode *node;
    find_last_parent(path, &parent);
    int inode_number = 0;
    inode_number = find_inode(path, &node);
    if (inode_number == -1)
        return -1;
    char *tmp = malloc(SECTOR_SIZE);
    struct file_record *tmp_file_record = malloc(sizeof(struct file_record));

    if (node->type == FILE_TYPE)
    {
        fprintf(stderr, "Use file unlink for files\n");
        free(parent);
        free(node);
        free(tmp);
        free(tmp_file_record);
        return -1;
    }

    //Check no file exists within dir
    int i;
    for (i = 0; i < DATA_BLOCK_PER_INODE; i++)
    {
        if (node->data_blocks[i] != 0)
        {
            Disk_Read(node->data_blocks[i], tmp);
            int j;
            for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
            {
                memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                if (tmp_file_record->inode_number != 0)
                {
                    fprintf(stderr, "Directory is not empty\n");
                    osErrno = E_DIR_NOT_EMPTY;
                    free(parent);
                    free(tmp);
                    free(tmp_file_record);
                    return -1;
                }

            }
        }
    }
    set_inode_bitmap(inode_number, 0);
    char found_entry_to_remove = 0;
    for (i = 0; i < DATA_BLOCK_PER_INODE && !found_entry_to_remove; i++)
    {
        if (parent->data_blocks[i] != 0)
        {
            Disk_Read(parent->data_blocks[i], tmp);
            int j;
            for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
            {
                memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                if (tmp_file_record->inode_number == inode_number)
                {
                    tmp_file_record->inode_number = 0;
                    memset(tmp_file_record->name, 0, sizeof(tmp_file_record->name));
                    memcpy(&tmp[j * sizeof(struct file_record)], tmp_file_record, sizeof(struct file_record));
                    Disk_Write(parent->data_blocks[i], tmp);
                    char is_whole_block_empty = 1;
                    int k;
                    for (k = 0; k < SECTOR_SIZE / sizeof(struct file_record); k++)
                    {
                        memcpy(tmp_file_record, &tmp[k * sizeof(struct file_record)], sizeof(struct file_record));
                        if (tmp_file_record->inode_number != 0)
                        {
                            is_whole_block_empty = 0;
                            break;
                        }
                    }
                    if (is_whole_block_empty)
                        set_datablock_bitmap(parent->data_blocks[i], 0);
                    found_entry_to_remove = 1;
                    break;
                }

            }
        }
    }
    free(parent);
    free(node);
    free(tmp);
    free(tmp_file_record);
    return 0;
}

int
File_Unlink(char *path)
{
    printf("File_Unlink\n");

    struct inode *parent;
    struct inode *node;
    int parent_inode_number = find_last_parent(path, &parent);
    int inode_number = find_inode(path, &node);
    if (parent_inode_number == -1 || inode_number == -1)
    {
        osErrno = E_NO_SUCH_FILE;
        return -1;
    }
    if (inode_open_count[inode_number] > 0)
    {
        osErrno = E_FILE_IN_USE;
        free(parent);
        free(node);
        return -1;
    }

    if (node->type == DIR_TYPE)
    {
        fprintf(stderr, "Use dir unlink for directories\n");
        return -1;
    }

    char *tmp = malloc(SECTOR_SIZE);
    struct file_record *tmp_file_record = malloc(sizeof(struct file_record));

    int i;
    for (i = 0; i < DATA_BLOCK_PER_INODE; i++)
    {
        if (node->data_blocks[i] != 0)
        {
            set_datablock_bitmap(node->data_blocks[i], 0);
        }
    }

    set_inode_bitmap(inode_number, 0);

    char found_entry_to_remove = 0;
    for (i = 0; i < DATA_BLOCK_PER_INODE && !found_entry_to_remove; i++)
    {
        if (parent->data_blocks[i] != 0)
        {
            Disk_Read(parent->data_blocks[i], tmp);
            int j;
            for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
            {
                memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                if (tmp_file_record->inode_number == inode_number)
                {
                    tmp_file_record->inode_number = 0;
                    memset(tmp_file_record->name, 0, sizeof(tmp_file_record->name));
                    memcpy(&tmp[j * sizeof(struct file_record)], tmp_file_record, sizeof(struct file_record));
                    Disk_Write(parent->data_blocks[i], tmp);
                    char is_whole_block_empty = 1;
                    int k;
                    for (k = 0; k < SECTOR_SIZE / sizeof(struct file_record); k++)
                    {
                        memcpy(tmp_file_record, &tmp[k * sizeof(struct file_record)], sizeof(struct file_record));
                        if (tmp_file_record->inode_number != 0)
                        {
                            is_whole_block_empty = 0;
                            break;
                        }
                    }
                    if (is_whole_block_empty)
                        set_datablock_bitmap(parent->data_blocks[i], 0);
                    found_entry_to_remove = 1;
                    break;
                }

            }
        }
    }
    free(node);
    free(parent);
    free(tmp);
    free(tmp_file_record);
    return 0;
}

// Tests

void test_initalize()
{
    unlink("test_image");
    FS_Boot("test_image");
}

void test_unlink()
{
    test_initalize();
    Dir_Create("/test_unlink");
    Dir_Create("/test_unlink/path2");
    Dir_Create("/test_unlink/path2/path3");
    Dir_Create("/test_unlink/path1");
    File_Create("/test_unlink/path1/1");
    File_Create("/test_unlink/path1/2");
    assert(Dir_Unlink("/test_unlink/path1/1") == -1);
    assert(File_Unlink("/test_unlink/path1") == -1);
    assert(Dir_Unlink("/test_unlink/path1") == -1);
    assert(Dir_Unlink("/test_unlink/path2") == -1);
    assert(Dir_Unlink("/test_unlink/path2/path3") == 0);
    assert(Dir_Unlink("/test_unlink/path2") == 0);
    assert(Dir_Unlink("/") == -1);
    assert(File_Unlink("/test_unlink/path1/1") == 0);
    assert(Dir_Unlink("/test_unlink/path1") == -1);
    assert(File_Unlink("/test_unlink/path1/2") == 0);
    assert(Dir_Unlink("/test_unlink/path1") == 0);
}

void test_single_sector()
{
    test_initalize();
    char test[] = "test";
    write_to_single_sector(0, 10, test, 4);
    read_from_single_sector(0, 11, test, 2);
    assert(test[0] == 'e');
    assert(test[1] == 's');
}

void test_read_write_seek()
{
    test_initalize();
    File_Create("/salam_test");
    int fd = File_Open("/salam_test");
    char str[] = "salam bar to";
    File_Write(fd, str, sizeof(str));
    File_Close(fd);
    fd = File_Open("/salam_test");
    char buff[15];
    File_Read(fd, buff, 5);
    buff[5] = '\0';
    assert(strcmp(buff, "salam") == 0);
    File_Seek(fd, 0);
    File_Read(fd, buff, 5);
    assert(strcmp(buff, "salam") == 0);
    assert(File_Read(fd, buff, 100) == 8);
    assert(strcmp(buff, " bar to") == 0);
}

void test_bitmap_inode()
{
    test_initalize();
    set_inode_bitmap(10, 1);
    set_inode_bitmap(11, 1);
    set_inode_bitmap(14, 0);
    set_inode_bitmap(15, 1);
    assert(get_inode_bitmap(10) == 1);
    assert(get_inode_bitmap(11) == 1);
    assert(get_inode_bitmap(14) == 0);
    assert(get_inode_bitmap(15) == 1);
}

void test_bitmap_block()
{
    test_initalize();
    set_inode_bitmap(4564, 1);
    set_inode_bitmap(4565, 1);
    set_inode_bitmap(4566, 0);
    set_inode_bitmap(4567, 1);
    assert(get_inode_bitmap(4564) == 1);
    assert(get_inode_bitmap(4565) == 1);
    assert(get_inode_bitmap(4566) == 0);
    assert(get_inode_bitmap(4567) == 1);
}

void test_file_folder_create()
{
    test_initalize();
    File_Unlink("/path");
    File_Unlink("/path2/salam");
    Dir_Unlink("/path2");
    assert(File_Create("/path") == 0);
    assert(File_Create("/path") == -1);
    assert(File_Create("/path2/salam") == -1);
    assert(Dir_Create("/path2") == 0);
    assert(File_Create("/path2/salam") == 0);
}

void test_file_in_use()
{
    test_initalize();
    File_Create("/using");
    int fd = File_Open("/using");
    assert(File_Unlink("/using") == -1);
    File_Close(fd);
    assert(File_Unlink("/using") == 0);

}

void test_open_file()
{
    test_initalize();
    File_Create("/file1");
    Dir_Create("/path2");
    File_Create("/file3");
    File_Create("/path2/1");
    File_Create("/path2/2");
    File_Create("/path2/3");
    File_Create("/path2/4");
    File_Create("/path2/5");
    assert(File_Open("/file3") == 0);
    assert(File_Open("/file3") == 1);
    assert(File_Open("/file3") == 2);
    File_Open("/file_dani");
    File_Open("/path2");
}

void test_dir_count()
{
    test_initalize();
    Dir_Create("/test_dir_count");
    File_Create("/test_dir_count/file1");
    Dir_Create("/test_dir_count/path2");
    File_Create("/test_dir_count/file3");
    File_Create("/test_dir_count/path2/1");
    File_Create("/test_dir_count/path2/2");
    File_Create("/test_dir_count/path2/3");
    File_Create("/test_dir_count/path2/4");
    File_Create("/test_dir_count/path2/5");
    char tmp5[1000];
    assert(Dir_Read("/test_dir_count/", tmp5, 1000) == 3);
    assert(Dir_Read("/test_dir_count/path2/", tmp5, 1000) == 5);
}

void test_max_fds()
{
    test_initalize();
    File_Create("/test_MAX_FDS");
    int i;
    for (i = 0; i < MAX_FDS; i++)
        assert(File_Open("/test_MAX_FDS") == i);
    assert(File_Open("/test_MAX_FDS") == -1);
    assert(osErrno == E_TOO_MANY_OPEN_FILES);
}

void test_no_space_left()
{
    test_initalize();
    int file_size = 512 * 30 - 400;
    char dummy[file_size];
    int files = (10000 - 256) / 30;
    char file_name[16];
    int file_num;
    for (file_num = 0; file_num < files + 1; file_num++)
    {
        sprintf(file_name, "/%d", file_num);
        assert(File_Create(file_name) == 0);
        int fd = File_Open(file_name);
        assert(fd != -1);
        assert(File_Write(fd, dummy, file_size) == 0);
        File_Close(fd);
    }

    sprintf(file_name, "/%d", file_num);
    assert(File_Create(file_name) == 0);
    int fd = File_Open(file_name);
    assert(fd != -1);
    assert(File_Write(fd, dummy, file_size) == -1);
    assert(osErrno == E_NO_SPACE);
}

void test_file_too_big()
{
    test_initalize();
    File_Create("/test_TOO_BIG");
    int fd = File_Open("/test_TOO_BIG");
    char str[512 * 30 - 5];
    File_Write(fd, str, sizeof(str));
    File_Close(fd);
    fd = File_Open("/test_TOO_BIG");
    File_Seek(fd, sizeof(str));
    char buff[] = "salamsalam";
    assert(File_Write(fd, buff, sizeof(buff)) == -1);
    assert(osErrno == E_FILE_TOO_BIG);
}

void test_all()
{
    test_file_too_big();
    test_max_fds();
//    test_no_space_left();
    test_file_folder_create();
    test_bitmap_block();
    test_single_sector();
    test_bitmap_inode();
    test_dir_count();
    test_open_file();
    test_read_write_seek();
    test_unlink();
    test_file_in_use();
    fprintf(stderr, "All tests passed\n");
}
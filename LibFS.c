#include <string.h>
#include <assert.h>
#include "LibFS.h"
#include "LibDisk.h"

//TODO: check free before any return

#define DATA_BLOCK_PER_INODE 30

const int MAGIC_NUMBER = 241543903;
const int MAX_FILES = 1000;
const int INODE_BITMAP_SIZE = 125; // MAX_FILES / BITS_IN_A_SINGLE_BYTE(8)
const int MAGIC_NUMBER_SIZE = 4;

typedef int SECTOR_NUM;


// global errno value here
int osErrno;

enum INODE_TYPE {
    DIR_TYPE, FILE_TYPE
};

struct inode {
    int size;
    int type;
    SECTOR_NUM data_blocks[DATA_BLOCK_PER_INODE];
};

struct file_record {
    char name[16];
    int inode_number;
};

void read_from_single_sector(int sector, int offset, void *buffer, size_t size) {
    char *tmp = malloc(SECTOR_SIZE);
    Disk_Read(sector, tmp);
    memcpy(buffer, &tmp[offset], size);
    free(tmp);
}

void write_to_single_sector(int sector, int offset, void *buffer, size_t size) {
    char *tmp = malloc(SECTOR_SIZE);
    Disk_Read(sector, tmp);
    memcpy(&tmp[offset], buffer, size);
    Disk_Write(sector, tmp);
    free(tmp);
}


//int read(int offset, void *buffer, int size) {
//    int sector_number = offset / SECTOR_SIZE;
//
//}

char *image_path = NULL;

int inode_number_to_sector_number(int inode_number) { return 1 + 3 + inode_number / 4; }

int inode_number_to_sector_offset(int inode_number) { return (int) ((inode_number % 4) * sizeof(struct inode)); }

void write_inode(int inode_number, struct inode *node) {
    write_to_single_sector(inode_number_to_sector_number(inode_number), inode_number_to_sector_offset(inode_number),
                           node,
                           sizeof(struct inode));
}

void read_inode(int inode_number, struct inode *node) {
    read_from_single_sector(inode_number_to_sector_number(inode_number), inode_number_to_sector_offset(inode_number),
                            node,
                            sizeof(struct inode));
}

void set_inode_bitmap(int inode_number, char value) {
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

char get_inode_bitmap(int inode_number) {
    int byte_position = MAGIC_NUMBER_SIZE + inode_number / 8;
    int byte_offset = inode_number % 8;
    char c;
    read_from_single_sector(0, byte_position, &c, 1);
    return (char) ((c >> byte_offset) & 1);
}

void set_datablock_bitmap(int block_number, char value) {
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

char get_datablock_bitmap(int block_number) {
    int sector = 1 + block_number / (SECTOR_SIZE * 8);
    int byte_position = (block_number % (SECTOR_SIZE * 8)) / 8;
    int byte_offset = (block_number % (SECTOR_SIZE * 8)) % 8;
    char c;
    read_from_single_sector(sector, byte_position, &c, 1);
    return (char) ((c >> byte_offset) & 1);
}

void initialize_filesystem(char *path, int *magic_number) {
    write_to_single_sector(0, 0, magic_number, 4);
    struct inode *root = calloc(1, sizeof(struct inode));
    root->size = 0;
    root->type = DIR_TYPE;
    set_inode_bitmap(0, 1);
    free(root);
    Disk_Save(path);
}

int
FS_Boot(char *path) {
    printf("FS_Boot %s\n", path);

    // oops, check for errors
    if (Disk_Init() == -1) {
        printf("Disk_Init() failed\n");
        osErrno = E_GENERAL;
        return -1;
    }

    int magic_number = MAGIC_NUMBER;
    if (Disk_Load(path) == -1) {
        if (diskErrno == E_OPENING_FILE) {
            initialize_filesystem(path, &magic_number);
            fprintf(stderr, "Filesystem didn't exist, creating new file\n");
        }
        else {
            osErrno = E_GENERAL;
            return -1;
        }
    }
    else {
        int tmp;
        read_from_single_sector(0, 0, &tmp, 4);
        if (tmp != magic_number) {
            osErrno = E_GENERAL;
            fprintf(stderr, "Magic number didn't match\n");
            return -1;
        }
    }


    // do all of the other stuff needed...
    image_path = path;
    return 0;
}


void test_single_sector() {
    char test[] = "test";
    write_to_single_sector(0, 10, test, 4);
    read_from_single_sector(0, 11, test, 2);
    assert(test[0] == 'e');
    assert(test[1] == 's');
}

void test_bitmap_inode() {
    set_inode_bitmap(10, 1);
    set_inode_bitmap(11, 1);
    set_inode_bitmap(14, 0);
    set_inode_bitmap(15, 1);
    assert(get_inode_bitmap(10) == 1);
    assert(get_inode_bitmap(11) == 1);
    assert(get_inode_bitmap(14) == 0);
    assert(get_inode_bitmap(15) == 1);
}

void test_bitmap_block() {
    set_inode_bitmap(4564, 1);
    set_inode_bitmap(4565, 1);
    set_inode_bitmap(4566, 0);
    set_inode_bitmap(4567, 1);
    assert(get_inode_bitmap(4564) == 1);
    assert(get_inode_bitmap(4565) == 1);
    assert(get_inode_bitmap(4566) == 0);
    assert(get_inode_bitmap(4567) == 1);
}

void test_file_folder_create() {//None of them should exists before
    char tmp[] = "/path";
    char tmp2[] = "/path2/salam";
    char tmp3[] = "/path2";
    assert(File_Create(tmp) == 0);
    assert(File_Create(tmp) == -1);
    assert(File_Create(tmp2) == -1);
    assert(Dir_Create(tmp3) == 0);
    assert(File_Create(tmp2) == 0);
}

void test_dir_count()
{
    File_Create("/file1");
    Dir_Create("/path2");
    File_Create("/file3");
    File_Create("/path2/1");
    File_Create("/path2/2");
    File_Create("/path2/3");
    File_Create("/path2/4");
    File_Create("/path2/5");
    char tmp5[1000];
    assert(Dir_Read("/", tmp5, 1000)==3);
    assert(Dir_Read("/path2/", tmp5, 1000)==5);
}

int get_new_block() {
    char *tmp = malloc(SECTOR_SIZE);
    int i;
    for (i = 1; i < 4; i++) {
        Disk_Read(i, tmp);
        int j = 0;
        if (i == 1)
            j = 254 / 8 + 1; // First 254 sectors are metadata
        for (; j < SECTOR_SIZE; j++) {
            int k;
            for (k = 0; k < 8; k++) {
                if ((tmp[j] & (1 << k)) == 0) {
                    tmp[j] |= (1 << k);
                    Disk_Write(i, tmp);
                    int sector_number = (i - 1) * SECTOR_SIZE * 8 + j * 8 + k;
                    memset(tmp, 0, sizeof tmp);
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

int get_new_inode(struct inode **new_node) {
    //TODO:Optimize this by reading the sector once
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (!get_inode_bitmap(i)) {
            set_inode_bitmap(i, 1);
            (*new_node) = calloc(1, sizeof(struct inode));
            write_inode(i, *new_node);
            return i;
        }
    }
    return -1;
}

int
FS_Sync() {
    printf("FS_Sync\n");
    Disk_Save(image_path);
    return 0;
}


int find_last_parent(char *file, struct inode **new_node) {
    struct inode *parent = calloc(1, sizeof(struct inode));
    int parent_inode_number = 0;
    read_inode(parent_inode_number, parent);
    if (strcmp(file, "/") == 0) {
        (*new_node) = parent;
        return parent_inode_number;
    }
    int start_pos = 1; //Ignoring the first slash
    int end_pos;
    char *tmp = malloc(SECTOR_SIZE);
    struct file_record *tmp_file_record = malloc(sizeof(struct file_record));
    while (1) {
        end_pos = start_pos;
        while (file[end_pos] != '\0' && file[end_pos] != '/')
            end_pos++;
        if (file[end_pos] == '\0') {
            (*new_node) = parent;
            return parent_inode_number;
        }
        if (start_pos == end_pos) {
            fprintf(stderr, "Wrong format of file path\n");
            osErrno = E_CREATE;
            return -1;
        }
        if (end_pos - start_pos >= 16) {
            fprintf(stderr, "File is longer than 16 character\n");
            osErrno = E_CREATE;
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
            for (i = 0; i < DATA_BLOCK_PER_INODE && !found_folder; i++) {
                if (parent->data_blocks[i] != 0) {
                    Disk_Read(parent->data_blocks[i], tmp);
                    int j;
                    for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++) {
                        memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                        if (strcmp(tmp_file_record->name, tmp_path) == 0) {
                            parent_inode_number = tmp_file_record->inode_number;
                            read_inode(parent_inode_number, parent);
                            if (parent->type != DIR_TYPE) {
                                fprintf(stderr, "Expected folder but found file: %s\n", tmp_path);
                                return -1;
                            }
                            found_folder = 1;
                            break;

                        }
                    }
                }
            }
            if (!found_folder) {
                fprintf(stderr, "Folder %s not found\n", tmp_path);
                return -1;
            }
        }

    }
}

int
File_Create(char *file) {
    return file_folder_create(file, FILE_TYPE);
}

int
file_folder_create(char *file, enum INODE_TYPE type) {
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
    if (start_pos == end_pos) {
        fprintf(stderr, "Create directory with Create_dir command\n");
        osErrno = E_CREATE;
        return -1;
    }
    if (end_pos - start_pos >= 16) {
        fprintf(stderr, "File is longer than 16 character\n");
        osErrno = E_CREATE;
        return -1;
    }
    char tmp_path[16];
    strncpy(tmp_path, &file[start_pos], end_pos - start_pos);
    tmp_path[end_pos - start_pos] = '\0';
    int i;
    for (i = 0; i < DATA_BLOCK_PER_INODE; i++) {
        if (parent->data_blocks[i] != 0) {
            Disk_Read(parent->data_blocks[i], tmp);
            int j;
            for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++) {
                memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                if (strcmp(tmp_file_record->name, tmp_path) == 0) {
                    fprintf(stderr, "File already exists\n");
                    osErrno = E_CREATE;
                    return -1;
                }
            }
        }
    }
    for (i = 0; i < DATA_BLOCK_PER_INODE; i++) {
        if (parent->data_blocks[i] == 0) {
            int block = get_new_block();
            if (block == -1)//no block available but there is still some chance
                continue;
            parent->data_blocks[i] = block;
        }
        Disk_Read(parent->data_blocks[i], tmp);
        int j;
        for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++) {
            memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
            if (tmp_file_record->inode_number == 0)//hooray! found free record
            {
                struct inode *new_node;
                int new_inode_number = get_new_inode(&new_node);
                if (new_inode_number == -1) {
                    fprintf(stderr, "No free inode available\n");
                    osErrno = E_CREATE;
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
                return 0;
            }
        }
    }
    //follow the path, if not exists error
    // append the file to the parent directory
    free(parent);
    return 0;
}

//int last_fd;
//int last_inode;
//int last_datablocks;
//struct file_descriptor{
//    int inode_number;
//    int pointer;
//}[MAX_OPEN_FILES];
//
//int open_count[MAX_TOTAL_FILES];
//
//inode read_inode(int i)
//{
//    calculate position
//    MAGIC_NUMBER_SIZE+inode_BITMAP+DATA_BITMAP+i*inode_SIZE
//    disk_read(folan_sector,);
//}
//
//inode_number follow_the_path(char* path)
//{
//    // follow the path
//    // start from inode number 0 which is ‘/‘
//    // iterate through the fucking inodes
//    //
//}


int
File_Open(char *file) {
    printf("FS_Open\n");

    // follow the path
    // find inode number
    // assign file descriptor in ram and
    // set pointer to beginning of file
    // open_count ++;
    // last_fd = (last_fd + 1) %MAX_OPEN_FILES;
    return 0;
}


int
File_Read(int fd, void *buffer, int size) {
    printf("FS_Read\n");
    //error handling
    // read  the inode (inode is in fd)
    // find which data block from fd pointer
    // read the data blocks until size
    // maybe more than one data block needed
    //delete the inode from ram
    return 0;
}

int
File_Write(int fd, void *buffer, int size) {
    printf("FS_Write\n");
    //error handling
    // read  the inode (inode is in fd)
    // find the size limit and set error if needed
    // find which data block from fd pointer
    // write the data blocks until size
    // error handle
    // maybe more than one data block needed
    //delete the inode from ram
    return 0;
}

int
File_Seek(int fd, int offset) {
    printf("FS_Seek\n");
    //seekdir
    return 0;
}

int
File_Close(int fd) {
    printf("FS_Close\n");
    // error handling
    //get the inode number
    // open_count[inode_number] —;
    // set the inode_number to 0
    return 0;
}

int
File_Unlink(char *file) {
    printf("FS_Unlink\n");
    //check open_count
    // more error handling
    //follow the path, if not exists error
    // delete the file from the parent directory
    // free the inode bit map and datablocks
    return 0;
}


// directory ops
int
Dir_Create(char *path) {
    printf("Dir_Create %s\n", path);
    //same as file but change the type to directory
    file_folder_create(path, DIR_TYPE);
    return 0;
}

int
Dir_Size(char *path) {
    printf("Dir_Size\n");
    //use follow_the_path function and recurse and sum up all the sizes
    return 0;
}

int
Dir_Read(char *path, void *buffer, int size) {
    printf("Dir_Read\n");
    int inode_number;
    struct inode *node;
    inode_number = find_last_parent(path, &node);
    struct file_record *tmp_file_record = malloc(sizeof(struct file_record));
    char *tmp = malloc(SECTOR_SIZE);
    if (inode_number == -1)
        return -1;
    int i;
    int entry_count = 0;
    for (i = 0; i < DATA_BLOCK_PER_INODE; i++) {
        if (node->data_blocks[i] != 0) {
            Disk_Read(node->data_blocks[i], tmp);
            int j;
            for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++) {
                memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
                if (tmp_file_record->inode_number == 0)continue;
                if (size - (int) sizeof(struct file_record) < 0) {
                    osErrno = E_BUFFER_TOO_SMALL;
                    return -1;
                }
                size -= (int) sizeof(struct file_record);
                memcpy(&buffer[entry_count * sizeof(struct file_record)], &tmp[j * sizeof(struct file_record)],
                       sizeof(struct file_record));
                entry_count++;


            }
        }
    }
    return entry_count;
}

int
Dir_Unlink(char *path) {
    printf("Dir_Unlink\n");
    // check if any file exists within if it is, error
    // remove from parent inode, free inode bitmap, free datablocks if any
    return 0;
}
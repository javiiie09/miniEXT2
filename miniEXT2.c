#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdint.h>
#include <time.h>

#define BLOCK_SIZE 4096
#define INODE_SIZE 128
#define MAX_NAME 28
#define DIRECT_POINTERS 12

// Estructuras del sistema de archivos
#pragma pack(push, 1)
struct superblock {
    uint32_t total_blocks;
    uint32_t total_inodes;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t first_data_block;
    char fs_name[16];
};

struct inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t blocks;
    uint32_t direct[12];
    uint32_t indirect;
    uint32_t ctime;
    uint32_t mtime;
    uint16_t links_count;
};

struct dir_entry {
    uint32_t inode;
    char name[MAX_NAME];
};
#pragma pack(pop)

// Variables globales
static void *fs_image = NULL;
static struct superblock *sb = NULL;
static struct inode *inode_table = NULL;

// Utilidades
struct inode *get_inode(int index) {
    return &inode_table[index];
}

void *get_block(int block_num) {
    return fs_image + block_num * BLOCK_SIZE;
}

// FUSE: Obtener atributos
static int minifs_getattr(const char *path, struct stat *st) {
    memset(st, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }

    struct inode *root = get_inode(0);
    int entries = root->size / sizeof(struct dir_entry);
    struct dir_entry *entries_ptr = (struct dir_entry *)get_block(root->direct[0]);

    for (int i = 0; i < entries; i++) {
        if (strcmp(entries_ptr[i].name, path + 1) == 0) {
            struct inode *node = get_inode(entries_ptr[i].inode);
            st->st_mode = (node->mode & S_IFDIR) ? S_IFDIR | 0755 : S_IFREG | 0444;
            st->st_nlink = node->links_count;
            st->st_size = node->size;
            st->st_mtime = node->mtime;
            return 0;
        }
    }

    return -ENOENT;
}

// FUSE: Leer directorio
static int minifs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi,
                          enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    if (strcmp(path, "/") != 0) return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    struct inode *root = get_inode(0);
    int entries = root->size / sizeof(struct dir_entry);
    struct dir_entry *entries_ptr = (struct dir_entry *)get_block(root->direct[0]);

    for (int i = 0; i < entries; i++) {
        filler(buf, entries_ptr[i].name, NULL, 0, 0);
    }

    return 0;
}

// FUSE: Leer archivo
static int minifs_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi) {
    struct inode *root = get_inode(0);
    int entries = root->size / sizeof(struct dir_entry);
    struct dir_entry *entries_ptr = (struct dir_entry *)get_block(root->direct[0]);

    for (int i = 0; i < entries; i++) {
        if (strcmp(entries_ptr[i].name, path + 1) == 0) {
            struct inode *node = get_inode(entries_ptr[i].inode);

            if (offset >= node->size) return 0;
            if (offset + size > node->size) size = node->size - offset;

            int block = node->direct[0];
            memcpy(buf, get_block(block) + offset, size);
            return size;
        }
    }

    return -ENOENT;
}

static int minifs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    struct inode *root = get_inode(0);
    struct dir_entry *entries = (struct dir_entry *)get_block(root->direct[0]);
    int max_entries = BLOCK_SIZE / sizeof(struct dir_entry);

    // Buscar un inodo libre
    int inode_index = -1;
    for (int i = 1; i < sb->total_inodes; i++) {
        struct inode *in = get_inode(i);
        if (in->mode == 0) {
            inode_index = i;
            break;
        }
    }
    if (inode_index == -1) return -ENOSPC;

    // Buscar una entrada de directorio libre
    for (int i = 0; i < max_entries; i++) {
        if (entries[i].inode == 0) {
            strncpy(entries[i].name, path + 1, MAX_NAME - 1);
            entries[i].name[MAX_NAME - 1] = '\0';
            entries[i].inode = inode_index;

            struct inode *new_inode = get_inode(inode_index);
            memset(new_inode, 0, sizeof(struct inode));
            new_inode->mode = S_IFREG | mode;
            new_inode->ctime = time(NULL);
            new_inode->mtime = time(NULL);
            new_inode->links_count = 1;

            root->size += sizeof(struct dir_entry);
            return 0;
        }
    }

    return -ENOSPC;
}

static int minifs_write(const char *path, const char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
    struct inode *root = get_inode(0);
    struct dir_entry *entries = (struct dir_entry *)get_block(root->direct[0]);
    int entries_count = root->size / sizeof(struct dir_entry);

    for (int i = 0; i < entries_count; i++) {
        if (strcmp(entries[i].name, path + 1) == 0) {
            struct inode *node = get_inode(entries[i].inode);

            if (node->direct[0] == 0) {
                // Asignar primer bloque de datos libre
                for (int b = sb->first_data_block; b < sb->total_blocks; b++) {
                    void *blk = get_block(b);
                    if (*(char *)blk == 0) {
                        node->direct[0] = b;
                        break;
                    }
                }
            }

            if (node->direct[0] == 0) return -ENOSPC;

            memcpy(get_block(node->direct[0]) + offset, buf, size);
            node->size = offset + size;
            node->mtime = time(NULL);
            return size;
        }
    }

    return -ENOENT;
}

static int minifs_unlink(const char *path) {
    struct inode *root = get_inode(0);
    struct dir_entry *entries = (struct dir_entry *)get_block(root->direct[0]);
    int entries_count = root->size / sizeof(struct dir_entry);

    for (int i = 0; i < entries_count; i++) {
        if (strcmp(entries[i].name, path + 1) == 0) {
            struct inode *node = get_inode(entries[i].inode);
            node->mode = 0; // Liberar inodo
            node->size = 0;
            node->links_count = 0;

            entries[i].inode = 0;
            memset(entries[i].name, 0, MAX_NAME);
            return 0;
        }
    }

    return -ENOENT;
}

static int minifs_mkdir(const char *path, mode_t mode) {
    return minifs_create(path, S_IFDIR | mode, NULL);
}

static int minifs_rmdir(const char *path) {
    struct inode *root = get_inode(0);
    struct dir_entry *entries = (struct dir_entry *)get_block(root->direct[0]);
    int entries_count = root->size / sizeof(struct dir_entry);

    for (int i = 0; i < entries_count; i++) {
        if (strcmp(entries[i].name, path + 1) == 0) {
            struct inode *node = get_inode(entries[i].inode);

            // Solo borrar si está vacío
            if (node->size == 0 && (node->mode & S_IFDIR)) {
                node->mode = 0;
                node->size = 0;
                node->links_count = 0;

                entries[i].inode = 0;
                memset(entries[i].name, 0, MAX_NAME);
                return 0;
            } else {
                return -ENOTEMPTY;
            }
        }
    }

    return -ENOENT;
}


// FUSE: Estructura principal
static struct fuse_operations minifs_oper = {
    .getattr = minifs_getattr,
    .readdir = minifs_readdir,
    .read    = minifs_read,
    .create  = minifs_create,
    .write   = minifs_write,
    .unlink  = minifs_unlink,
    .mkdir   = minifs_mkdir,
    .rmdir   = minifs_rmdir,
};


// main
int main(int argc, char *argv[]) {
    int fd = open("imagen.bin", O_RDWR);
    if (fd < 0) {
        perror("Error al abrir imagen.bin");
        return 1;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    fs_image = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fs_image == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    sb = (struct superblock *)fs_image;
    int inode_table_start = 3 * BLOCK_SIZE; // después de superbloque y bitmaps
    inode_table = (struct inode *)(fs_image + inode_table_start);

    return fuse_main(argc, argv, &minifs_oper, NULL);
}

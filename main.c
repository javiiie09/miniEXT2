#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define BLOCK_SIZE 4096
#define INODE_SIZE 128
#define MAX_NAME 28
#define DIRECT_POINTERS 12
#define TOTAL_BLOCKS 128
#define TOTAL_INODES 16

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

int main() {
    FILE *f = fopen("imagen", "wb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    // Reservar tamaño total de la imagen
    ftruncate(fileno(f), TOTAL_BLOCKS * BLOCK_SIZE);
    uint8_t *image = calloc(TOTAL_BLOCKS, BLOCK_SIZE);

    // SUPERBLOQUE
    struct superblock *sb = (struct superblock *)image;
    sb->total_blocks = TOTAL_BLOCKS;
    sb->total_inodes = TOTAL_INODES;
    sb->block_size = BLOCK_SIZE;
    sb->inode_size = INODE_SIZE;
    sb->free_blocks = TOTAL_BLOCKS - 4;
    sb->free_inodes = TOTAL_INODES - 2;
    sb->first_data_block = 4;
    strncpy(sb->fs_name, "MiniExt2FS", 15);

    // INODE TABLE (bloque 3)
    struct inode *inode_table = (struct inode *)(image + BLOCK_SIZE * 3);

    // INODO 0: directorio raíz
    struct inode *root = &inode_table[0];
    root->mode = 0x4000; // S_IFDIR
    root->ctime = root->mtime = time(NULL);
    root->links_count = 1;
    root->size = sizeof(struct dir_entry); // solo un archivo
    root->direct[0] = 4; // primer bloque de datos

    // INODO 1: archivo hello.txt
    struct inode *file = &inode_table[1];
    file->mode = 0x8000; // S_IFREG
    file->ctime = file->mtime = time(NULL);
    file->links_count = 1;
    const char *msg = "Hola MiniFS!\n";
    file->size = strlen(msg);
    file->direct[0] = 5; // segundo bloque de datos

    // BLOQUE DE DIRECTORIO (bloque 4)
    struct dir_entry *de = (struct dir_entry *)(image + BLOCK_SIZE * 4);
    de[0].inode = 1;
    strncpy(de[0].name, "hello.txt", MAX_NAME - 1);

    // BLOQUE DE DATOS DEL ARCHIVO (bloque 5)
    memcpy(image + BLOCK_SIZE * 5, msg, strlen(msg));

    // Guardar la imagen
    fwrite(image, BLOCK_SIZE, TOTAL_BLOCKS, f);
    fclose(f);
    free(image);

    printf("Imagen 'imagen.bin' creada correctamente.\n");
    return 0;
}

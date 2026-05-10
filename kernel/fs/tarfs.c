#include <fs/tarfs.h>
#include <rexos/fs.h>
#include <lib/string.h>
#include <mm/heap.h>
#include <lib/printf.h>

/* USTAR formátumú TAR fejléc (512 byte) */
struct tar_header {
    char filename[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};

/* Segédfüggvény: Oktális string konvertálása számmá */
static uint64_t octal_to_int(const char *str, int size) {
    uint64_t n = 0;
    for (int i = 0; i < size; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            n = n * 8 + (str[i] - '0');
        }
    }
    return n;
}

/* Fájl beolvasása a memóriából */
static uint64_t tarfs_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (offset >= node->length) return 0;
    if (offset + size > node->length) {
        size = node->length - offset;
    }
    
    uint8_t *file_data = (uint8_t *)node->ptr;
    kmemcpy(buffer, file_data + offset, size);
    return size;
}

#define TARFS_MAX_NODES 256

/* A letisztult (Phase 10) implementációhoz egy egyszerű listát használunk
 * a memóriába betöltött fájlok tárolására. */
static vfs_node_t *tarfs_nodes[TARFS_MAX_NODES];
static uint32_t tarfs_node_count = 0;
static dirent_t tarfs_dirent;

/* Gyökérkönyvtár olvasása (iterálás a fájlokon) */
static dirent_t *tarfs_readdir(vfs_node_t *node, uint32_t index) {
    (void)node; // A root csomópont
    if (index >= tarfs_node_count) return NULL;
    
    kstrncpy(tarfs_dirent.name, tarfs_nodes[index]->name, 127);
    tarfs_dirent.ino = tarfs_nodes[index]->inode;
    return &tarfs_dirent;
}

/* Fájl keresése a gyökérkönyvtárban név alapján */
static vfs_node_t *tarfs_finddir(vfs_node_t *node, const char *name) {
    (void)node; // A root csomópont
    for (uint32_t i = 0; i < tarfs_node_count; i++) {
        if (kstrcmp(tarfs_nodes[i]->name, name) == 0) {
            return tarfs_nodes[i];
        }
    }
    return NULL;
}

void tarfs_init(uint64_t address, uint64_t size) {
    uint64_t offset = 0;
    
    /* VFS Gyökér csomópont (/) inicializálása */
    vfs_node_t *root = kzalloc(sizeof(vfs_node_t));
    if (!root) {
        kprintf("[tarfs] Error: out of memory for root node\n");
        return;
    }
    
    kstrcpy(root->name, "tarfs_root");
    root->flags = FS_DIRECTORY;
    root->readdir = tarfs_readdir;
    root->finddir = tarfs_finddir;
    fs_root = root;
    
    /* TAR Archívum bejárása */
    while (offset < size) {
        struct tar_header *header = (struct tar_header *)(address + offset);
        
        /* Üres fájlnév jelzi az archívum végét */
        if (header->filename[0] == '\0') {
            break;
        }
        
        uint64_t file_size = octal_to_int(header->size, 11);
        
        /* Csak normál fájlokat dolgozunk fel most (típus 0 vagy '0')
         * Mappák bejegyzéseit (typeflag == '5') most átlépjük a flat struktúra miatt. */
        if (header->typeflag == '0' || header->typeflag == '\0') {
            vfs_node_t *node = kzalloc(sizeof(vfs_node_t));
            if (!node) break;
            
            /* Tisztítsuk meg a fájlnevet a './' előtagtól, ha a tar úgy készítette */
            const char *name = header->filename;
            if (name[0] == '.' && name[1] == '/') {
                name += 2;
            }
            
            kstrncpy(node->name, name, 127);
            node->length = file_size;
            node->flags = FS_FILE;
            node->inode = tarfs_node_count;
            
            /* Az adatok a 512 byte-os fejléc után kezdődnek */
            node->ptr = (struct vfs_node *)(address + offset + 512);
            node->read = tarfs_read;
            
            if (tarfs_node_count < TARFS_MAX_NODES) {
                tarfs_nodes[tarfs_node_count++] = node;
                kprintf("[tarfs] Loaded file: %s (%lu bytes)\n", node->name, file_size);
            }
        }
        
        /* A blokk mérete a fejléc (512 byte) + az adatok mérete 512-es határra kerekítve */
        uint64_t blocks = (file_size + 511) / 512;
        offset += 512 + blocks * 512;
    }
    
    kprintf("[tarfs] Successfully mounted TAR Initrd (%u files)\n", tarfs_node_count);
}

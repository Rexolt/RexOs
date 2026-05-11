/* Rex OS - Egyszerű FAT32 (read-only) driver
 *
 * Mit tud:
 *   - LBA0-tól indul: lehet sima FAT32 (boot szektor mindjárt LBA0),
 *     vagy MBR-rel partícionált lemez (LBA0 = MBR, partíció start lookup).
 *   - 8.3 fájlnevek + alap LFN (Long File Name) támogatás.
 *   - Könyvtárak rekurzívan, alkönyvtárak is.
 *   - Olvasás: cluster-lánc követéssel.
 *
 * Mit NEM tud (még):
 *   - Írás
 *   - FAT12/FAT16
 *   - Időbélyegek
 */

#include <fs/fat32.h>
#include <drivers/ata/ata.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/printf.h>

#define SECTOR_SIZE 512
#define CLUSTER_END_MARK 0x0FFFFFF8

/* --- Boot szektor (BPB) ----------------------------------------------- */
#pragma pack(push, 1)
typedef struct {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries_16;   /* FAT12/16: gyökér bejegyzések; FAT32: 0 */
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;       /* FAT12/16; FAT32: 0 */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    /* FAT32 EBPB */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];        /* "FAT32   " */
    uint8_t  boot_code[420];
    uint16_t signature;         /* 0xAA55 */
} fat32_bpb_t;

typedef struct {
    uint8_t  name[11];          /* 8.3 nem null-terminált */
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  ctime_tenth;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cluster_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t cluster_lo;
    uint32_t size;
} fat32_dir_entry_t;

typedef struct {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t cluster_zero;
    uint16_t name3[2];
} fat32_lfn_entry_t;
#pragma pack(pop)

/* MBR partíció bejegyzés (16 byte), 4 db az LBA0 offset 0x1BE-ban */
#pragma pack(push, 1)
typedef struct {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_first;
    uint32_t total_sectors;
} mbr_partition_t;
#pragma pack(pop)

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LFN       0x0F /* 1|2|4|8 */

/* --- Globális FAT állapot --------------------------------------------- */

static struct {
    bool     mounted;
    uint32_t partition_lba;     /* a FAT32 boot szektor offsetje LBA-ban */
    uint32_t fat_start_lba;     /* első FAT első szektora */
    uint32_t cluster_begin_lba; /* data area kezdő szektora */
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t root_cluster;
    uint32_t fat_size;          /* szektorokban */
} g_fat;

/* --- FAT cluster lánc --------------------------------------------------- */

/* Cluster-szám -> első szektor (LBA) a data area-ban. */
static uint32_t cluster_to_lba(uint32_t cluster) {
    return g_fat.cluster_begin_lba + (cluster - 2) * g_fat.sectors_per_cluster;
}

/* Következő cluster a láncban, vagy >= 0x0FFFFFF8 ha vége. */
static uint32_t fat_next_cluster(uint32_t cluster) {
    uint32_t fat_offset    = cluster * 4;
    uint32_t fat_sector    = g_fat.fat_start_lba + (fat_offset / SECTOR_SIZE);
    uint32_t ent_offset    = (fat_offset % SECTOR_SIZE) / 4;
    static uint8_t buf[SECTOR_SIZE];
    if (ata_read_sectors(fat_sector, 1, buf) != 1) return 0x0FFFFFFF;
    uint32_t *entries = (uint32_t *)buf;
    return entries[ent_offset] & 0x0FFFFFFF;
}

/* Cluster teljes tartalmát olvassa ki egy frissen kmalloc-olt bufferbe. */
static uint8_t *read_cluster(uint32_t cluster) {
    uint8_t *buf = (uint8_t *)kmalloc(g_fat.bytes_per_cluster);
    if (!buf) return NULL;
    uint32_t lba = cluster_to_lba(cluster);
    /* count max 256-os blokkokra bontva */
    uint32_t remaining = g_fat.sectors_per_cluster;
    uint8_t *p = buf;
    while (remaining > 0) {
        uint32_t chunk = remaining > 32 ? 32 : remaining;
        if (ata_read_sectors(lba, (uint8_t)chunk, p) != chunk) {
            kfree(buf);
            return NULL;
        }
        lba += chunk;
        p += chunk * SECTOR_SIZE;
        remaining -= chunk;
    }
    return buf;
}

/* --- 8.3 név formázás ---------------------------------------------------- */

static void name_8_3_to_string(const uint8_t raw[11], char *out) {
    int o = 0;
    /* alapnév */
    for (int i = 0; i < 8; i++) {
        if (raw[i] == ' ') break;
        char c = (char)raw[i];
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
        out[o++] = c;
    }
    /* kiterjesztés */
    if (raw[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11; i++) {
            if (raw[i] == ' ') break;
            char c = (char)raw[i];
            if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
            out[o++] = c;
        }
    }
    out[o] = 0;
}

/* --- VFS node-ok ----- */

#define FAT32_MAX_NODES 256
static vfs_node_t *g_fat_nodes_pool[FAT32_MAX_NODES];
static uint32_t    g_fat_node_count = 0;

typedef struct {
    uint32_t start_cluster;
    uint32_t size;
    bool     is_dir;
    /* Csak könyvtárakhoz: cache-elt gyermek lista (lazy) */
    vfs_node_t **children;
    uint32_t    child_count;
    bool        children_loaded;
} fat32_priv_t;

static fat32_priv_t *priv_of(vfs_node_t *n) { return (fat32_priv_t *)n->ptr; }

/* Forward */
static void fat32_dir_load_children(vfs_node_t *dir);

static dirent_t g_fat_dirent;

static dirent_t *fat32_readdir(vfs_node_t *node, uint32_t index) {
    if (!node || (node->flags & 0x07) != FS_DIRECTORY) return NULL;
    fat32_priv_t *p = priv_of(node);
    if (!p) return NULL;
    if (!p->children_loaded) fat32_dir_load_children(node);
    if (index >= p->child_count) return NULL;
    kstrncpy(g_fat_dirent.name, p->children[index]->name, 127);
    g_fat_dirent.ino = p->children[index]->inode;
    return &g_fat_dirent;
}

static vfs_node_t *fat32_finddir(vfs_node_t *node, const char *name) {
    if (!node || (node->flags & 0x07) != FS_DIRECTORY) return NULL;
    fat32_priv_t *p = priv_of(node);
    if (!p) return NULL;
    if (!p->children_loaded) fat32_dir_load_children(node);
    for (uint32_t i = 0; i < p->child_count; i++) {
        if (kstrcmp(p->children[i]->name, name) == 0) return p->children[i];
    }
    return NULL;
}

static uint64_t fat32_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (!node) return 0;
    fat32_priv_t *p = priv_of(node);
    if (!p || p->is_dir) return 0;
    if (offset >= p->size) return 0;
    if (offset + size > p->size) size = p->size - offset;

    uint64_t out_pos = 0;
    uint32_t cluster = p->start_cluster;
    uint64_t cluster_offset = 0;

    /* Skip clusterek az offsetig */
    while (cluster_offset + g_fat.bytes_per_cluster <= offset) {
        cluster = fat_next_cluster(cluster);
        if (cluster >= CLUSTER_END_MARK || cluster < 2) return 0;
        cluster_offset += g_fat.bytes_per_cluster;
    }

    while (out_pos < size && cluster < CLUSTER_END_MARK && cluster >= 2) {
        uint8_t *cbuf = read_cluster(cluster);
        if (!cbuf) break;

        uint64_t in_cluster_offset = (out_pos == 0) ? (offset - cluster_offset) : 0;
        uint64_t copy_len = g_fat.bytes_per_cluster - in_cluster_offset;
        if (copy_len > size - out_pos) copy_len = size - out_pos;

        kmemcpy(buffer + out_pos, cbuf + in_cluster_offset, copy_len);
        kfree(cbuf);

        out_pos += copy_len;
        cluster_offset += g_fat.bytes_per_cluster;
        cluster = fat_next_cluster(cluster);
    }

    return out_pos;
}

static vfs_node_t *create_fat32_node(const char *name, bool is_dir, uint32_t cluster, uint32_t size) {
    vfs_node_t *n = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!n) return NULL;
    fat32_priv_t *p = (fat32_priv_t *)kzalloc(sizeof(fat32_priv_t));
    if (!p) { kfree(n); return NULL; }

    kstrncpy(n->name, name, 127);
    n->flags  = is_dir ? FS_DIRECTORY : FS_FILE;
    n->length = size;
    n->inode  = g_fat_node_count;
    n->ptr    = (vfs_node_t *)p;

    p->start_cluster = cluster;
    p->size  = size;
    p->is_dir = is_dir;
    p->children = NULL;
    p->child_count = 0;
    p->children_loaded = false;

    n->read = fat32_read;
    n->readdir = fat32_readdir;
    n->finddir = fat32_finddir;

    if (g_fat_node_count < FAT32_MAX_NODES) {
        g_fat_nodes_pool[g_fat_node_count++] = n;
    }
    return n;
}

/* Egy LFN bejegyzésből kiolvassa a 13 UCS-2 karaktert egy lokális buffer adott pozíciójára. */
static void lfn_extract(const fat32_lfn_entry_t *l, char *dst) {
    int o = 0;
    for (int i = 0; i < 5; i++) dst[o++] = (l->name1[i] < 256) ? (char)l->name1[i] : '?';
    for (int i = 0; i < 6; i++) dst[o++] = (l->name2[i] < 256) ? (char)l->name2[i] : '?';
    for (int i = 0; i < 2; i++) dst[o++] = (l->name3[i] < 256) ? (char)l->name3[i] : '?';
}

/* Könyvtár tartalmának teljes beolvasása és node-ok létrehozása. */
static void fat32_dir_load_children(vfs_node_t *dir) {
    fat32_priv_t *dp = priv_of(dir);
    if (!dp || dp->children_loaded) return;
    dp->children_loaded = true;

    /* Először count: hány bejegyzés lesz max (cluster lánc * (bytes_per_cluster/32)) */
    /* Egyszerűbb: dinamikusan növelünk egy ideiglenes pointer-tömböt. */
    vfs_node_t **kids = NULL;
    uint32_t kids_cap = 0, kids_n = 0;

    char lfn_buf[260];
    int lfn_have = 0;
    int lfn_pos = 0;  /* hány karakter rakódott a bufferbe (felülről lefelé töltjük) */

    uint32_t cluster = dp->start_cluster;
    if (cluster < 2 || cluster >= CLUSTER_END_MARK) return;

    while (cluster < CLUSTER_END_MARK && cluster >= 2) {
        uint8_t *cbuf = read_cluster(cluster);
        if (!cbuf) break;

        uint32_t entries = g_fat.bytes_per_cluster / 32;
        for (uint32_t i = 0; i < entries; i++) {
            fat32_dir_entry_t *e = (fat32_dir_entry_t *)(cbuf + i * 32);

            /* 0x00 = nincs több bejegyzés ezután */
            if (e->name[0] == 0x00) goto done;
            /* 0xE5 = törölt */
            if (e->name[0] == 0xE5) { lfn_have = 0; continue; }

            if ((e->attr & ATTR_LFN) == ATTR_LFN) {
                fat32_lfn_entry_t *l = (fat32_lfn_entry_t *)e;
                uint8_t ord = l->order & 0x1F;
                /* Ha első LFN bejegyzés (legmagasabb sorrend) tisztítsuk a buffert */
                if (l->order & 0x40) {
                    for (int k = 0; k < 260; k++) lfn_buf[k] = 0;
                    lfn_have = 1;
                    lfn_pos  = ord * 13;
                }
                char tmp[13];
                lfn_extract(l, tmp);
                int target = (ord - 1) * 13;
                for (int k = 0; k < 13; k++) lfn_buf[target + k] = tmp[k];
                continue;
            }

            /* Volume label kihagy */
            if (e->attr & ATTR_VOLUME_ID) { lfn_have = 0; continue; }

            char name[256];
            if (lfn_have) {
                /* Vágjuk az LFN-t az első null karakternél */
                int j = 0;
                while (j < 256 && lfn_buf[j]) { name[j] = lfn_buf[j]; j++; }
                name[j] = 0;
                lfn_have = 0;
                (void)lfn_pos;
            } else {
                name_8_3_to_string(e->name, name);
            }

            /* "." és ".." kihagy */
            if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
            if (name[0] == 0) continue;

            uint32_t first_cluster = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
            bool is_dir = (e->attr & ATTR_DIRECTORY) != 0;
            uint32_t size = is_dir ? 0 : e->size;

            vfs_node_t *child = create_fat32_node(name, is_dir, first_cluster, size);
            if (!child) continue;

            /* Adjuk a kids listához (egyszerű double-on-grow) */
            if (kids_n == kids_cap) {
                uint32_t new_cap = kids_cap ? kids_cap * 2 : 8;
                vfs_node_t **new_arr = (vfs_node_t **)kmalloc(new_cap * sizeof(*new_arr));
                if (!new_arr) { kfree(cbuf); goto done; }
                for (uint32_t k = 0; k < kids_n; k++) new_arr[k] = kids[k];
                if (kids) kfree(kids);
                kids = new_arr;
                kids_cap = new_cap;
            }
            kids[kids_n++] = child;
        }

        kfree(cbuf);
        cluster = fat_next_cluster(cluster);
    }

done:
    dp->children = kids;
    dp->child_count = kids_n;
}

/* --- Init ---------------------------------------------------------------- */

static uint32_t find_fat32_partition(void) {
    /* Olvassuk be az LBA0-t. Ha boot szektor sig 0xAA55 + fs_type "FAT32",
     * akkor a teljes lemez egy FAT32 fájlrendszer (LBA 0-tól indul).
     * Egyébként MBR-t feltételezünk és keresünk egy FAT32 partíciót. */
    uint8_t buf[SECTOR_SIZE];
    if (ata_read_sectors(0, 1, buf) != 1) return 0xFFFFFFFF;

    fat32_bpb_t *bpb = (fat32_bpb_t *)buf;
    if (bpb->signature == 0xAA55 &&
        bpb->bytes_per_sector == 512 &&
        bpb->fs_type[0] == 'F' && bpb->fs_type[1] == 'A' &&
        bpb->fs_type[2] == 'T' && bpb->fs_type[3] == '3' &&
        bpb->fs_type[4] == '2') {
        return 0;
    }

    /* MBR partíciók: 0x1BE offsettől 4 db, mindegyik 16 byte */
    mbr_partition_t *parts = (mbr_partition_t *)(buf + 0x1BE);
    for (int i = 0; i < 4; i++) {
        if (parts[i].type == 0x0B || parts[i].type == 0x0C ||
            parts[i].type == 0x1B || parts[i].type == 0x1C) {
            return parts[i].lba_first;
        }
    }
    return 0xFFFFFFFF;
}

vfs_node_t *fat32_init(void) {
    if (!ata_sector_count()) {
        kprintf("[fat32] No disk available\n");
        return NULL;
    }

    uint32_t part_lba = find_fat32_partition();
    if (part_lba == 0xFFFFFFFF) {
        kprintf("[fat32] No FAT32 partition found on disk\n");
        return NULL;
    }

    uint8_t bs_buf[SECTOR_SIZE];
    if (ata_read_sectors(part_lba, 1, bs_buf) != 1) {
        kprintf("[fat32] Failed to read boot sector at LBA %u\n", part_lba);
        return NULL;
    }
    fat32_bpb_t *bpb = (fat32_bpb_t *)bs_buf;

    if (bpb->bytes_per_sector != 512) {
        kprintf("[fat32] Unsupported sector size %u\n", bpb->bytes_per_sector);
        return NULL;
    }
    if (bpb->fat_size_32 == 0) {
        kprintf("[fat32] Not FAT32 (fat_size_32==0)\n");
        return NULL;
    }

    g_fat.mounted = true;
    g_fat.partition_lba = part_lba;
    g_fat.fat_size = bpb->fat_size_32;
    g_fat.fat_start_lba = part_lba + bpb->reserved_sectors;
    g_fat.cluster_begin_lba = g_fat.fat_start_lba + bpb->num_fats * bpb->fat_size_32;
    g_fat.sectors_per_cluster = bpb->sectors_per_cluster;
    g_fat.bytes_per_cluster = bpb->sectors_per_cluster * SECTOR_SIZE;
    g_fat.root_cluster = bpb->root_cluster;

    kprintf("[fat32] OK. part_lba=%u fat_start=%u data=%u spc=%u root=%u\n",
            part_lba, g_fat.fat_start_lba, g_fat.cluster_begin_lba,
            g_fat.sectors_per_cluster, g_fat.root_cluster);

    /* Volume label kiszedés */
    char vol[12];
    for (int i = 0; i < 11; i++) vol[i] = (char)bpb->volume_label[i];
    vol[11] = 0;
    /* Trailing space cut */
    for (int i = 10; i >= 0 && (vol[i] == ' ' || vol[i] == 0); i--) vol[i] = 0;

    vfs_node_t *root = create_fat32_node(vol[0] ? vol : "fat32", true, g_fat.root_cluster, 0);
    return root;
}

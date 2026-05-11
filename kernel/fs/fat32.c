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
#include <drivers/block/block.h>
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
    bool             mounted;
    block_device_t  *bdev;       /* alacsonyabb szintű blokk eszköz */
    uint32_t partition_lba;     /* a FAT32 boot szektor offsetje LBA-ban */
    uint32_t fat_start_lba;     /* első FAT első szektora */
    uint32_t cluster_begin_lba; /* data area kezdő szektora */
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t root_cluster;
    uint32_t fat_size;          /* szektorokban */
} g_fat;

/* Helper: szektor olvasás a kiválasztott block device-ról. */
static int read_blocks(uint64_t lba, uint32_t count, void *buf) {
    if (!g_fat.bdev || !g_fat.bdev->read) return 0;
    return g_fat.bdev->read(g_fat.bdev, lba, count, buf);
}

/* Helper: szektor írás a kiválasztott block device-ra. */
static int write_blocks(uint64_t lba, uint32_t count, const void *buf) {
    if (!g_fat.bdev || !g_fat.bdev->write) return 0;
    return g_fat.bdev->write(g_fat.bdev, lba, count, buf);
}

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
    if (read_blocks(fat_sector, 1, buf) != 1) return 0x0FFFFFFF;
    uint32_t *entries = (uint32_t *)buf;
    return entries[ent_offset] & 0x0FFFFFFF;
}

/* FAT entry írása (MINDKÉT FAT példányba). */
static void fat_set_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset  = cluster * 4;
    uint32_t fat_sector  = g_fat.fat_start_lba + (fat_offset / SECTOR_SIZE);
    uint32_t ent_offset  = fat_offset % SECTOR_SIZE;

    static uint8_t wbuf[SECTOR_SIZE];
    for (uint8_t fi = 0; fi < (g_fat.fat_size > 0 ? 2 : 1); fi++) {
        uint32_t sec = fat_sector + fi * g_fat.fat_size;
        if (read_blocks(sec, 1, wbuf) != 1) return;
        uint32_t *e = (uint32_t *)(wbuf + ent_offset);
        *e = (*e & 0xF0000000u) | (value & 0x0FFFFFFFu);
        write_blocks(sec, 1, wbuf);
    }
}

/* Szabad cluster keresés és foglalás. Visszaad 0-t ha nincs szabad hely. */
static uint32_t fat_alloc_cluster(void) {
    static uint8_t fat_sec_buf[SECTOR_SIZE];
    uint32_t entries_per_sec = SECTOR_SIZE / 4;

    for (uint32_t sec = 0; sec < g_fat.fat_size; sec++) {
        if (read_blocks(g_fat.fat_start_lba + sec, 1, fat_sec_buf) != 1) continue;
        uint32_t *entries = (uint32_t *)fat_sec_buf;
        for (uint32_t j = 0; j < entries_per_sec; j++) {
            if ((entries[j] & 0x0FFFFFFF) == 0) {
                uint32_t cluster = sec * entries_per_sec + j;
                if (cluster < 2) continue;  /* cluster 0 és 1 speciális */
                /* Foglalás: EOC jel */
                entries[j] = (entries[j] & 0xF0000000u) | 0x0FFFFFFFFu;
                write_blocks(g_fat.fat_start_lba + sec, 1, fat_sec_buf);
                /* Második FAT frissítése ha van */
                if (g_fat.fat_size > 0 && read_blocks(g_fat.fat_start_lba + g_fat.fat_size + sec, 1, fat_sec_buf) == 1) {
                    entries[j] = (entries[j] & 0xF0000000u) | 0x0FFFFFFFFu;
                    write_blocks(g_fat.fat_start_lba + g_fat.fat_size + sec, 1, fat_sec_buf);
                }
                return cluster;
            }
        }
    }
    return 0;  /* nincs szabad cluster */
}

/* Teljes cluster lánc felszabadítása. */
static void fat_free_chain(uint32_t cluster) {
    while (cluster >= 2 && cluster < CLUSTER_END_MARK) {
        uint32_t next = fat_next_cluster(cluster);
        fat_set_entry(cluster, 0);
        cluster = next;
    }
}

/* Egy cluster adatának kiírása a lemezre. */
static int fat_write_cluster(uint32_t cluster, const uint8_t *data) {
    uint32_t lba = cluster_to_lba(cluster);
    uint32_t remaining = g_fat.sectors_per_cluster;
    const uint8_t *p = data;
    while (remaining > 0) {
        uint32_t chunk = remaining > 32 ? 32 : remaining;
        if ((uint32_t)write_blocks(lba, chunk, p) != chunk) return -1;
        lba += chunk;
        p += chunk * SECTOR_SIZE;
        remaining -= chunk;
    }
    return 0;
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
        if ((uint32_t)read_blocks(lba, chunk, p) != chunk) {
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
    uint32_t    children_cap;     /* allokált kapacitás */
    bool        children_loaded;
    /* Íráshoz: a directory entry helye a szülő könyvtárban */
    uint32_t    dirent_sector;    /* LBA of parent dir sector containing this node's 8.3 entry */
    uint32_t    dirent_off;       /* byte offset within dirent_sector (multiple of 32) */
} fat32_priv_t;

/* Forward deklarációk (write API) */
static vfs_node_t *fat32_create_impl(vfs_node_t *dir, const char *name, uint32_t flags);
static int         fat32_mkdir_impl (vfs_node_t *dir, const char *name);
static int         fat32_unlink_impl(vfs_node_t *dir, const char *name);
static uint64_t    fat32_write(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);

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

/* A directory entry méret és cluster mezőinek frissítése a lemezen. */
static void fat32_update_dirent(vfs_node_t *node, uint32_t new_size, uint32_t new_cluster) {
    fat32_priv_t *p = priv_of(node);
    if (!p || p->dirent_sector == 0) return;

    static uint8_t sec_buf[SECTOR_SIZE];
    if (read_blocks(p->dirent_sector, 1, sec_buf) != 1) return;
    fat32_dir_entry_t *e = (fat32_dir_entry_t *)(sec_buf + p->dirent_off);
    if (new_size != (uint32_t)-1) {
        e->size = new_size;
    }
    if (new_cluster != (uint32_t)-1) {
        e->cluster_hi = (uint16_t)(new_cluster >> 16);
        e->cluster_lo = (uint16_t)(new_cluster & 0xFFFF);
    }
    write_blocks(p->dirent_sector, 1, sec_buf);
}

/* VFS write callback: adatot ír egy FAT32 fájlba. */
static uint64_t fat32_write(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (!node) return 0;
    fat32_priv_t *p = priv_of(node);
    if (!p || p->is_dir) return 0;
    if (size == 0) return 0;

    /* Ha a fájlnak nincs még clustere, foglalunk egyet */
    if (p->start_cluster < 2) {
        uint32_t nc = fat_alloc_cluster();
        if (!nc) return 0;
        p->start_cluster = nc;
        /* Töröljük az új clustert */
        uint8_t *zeros = (uint8_t *)kzalloc(g_fat.bytes_per_cluster);
        if (zeros) { fat_write_cluster(nc, zeros); kfree(zeros); }
        fat32_update_dirent(node, (uint32_t)-1, nc);
    }

    uint64_t out_pos = 0;
    uint32_t cluster = p->start_cluster;
    uint64_t cluster_offset = 0;
    uint32_t prev_cluster = 0;

    /* Seek az offset-ig, cluster lánc bővítéssel ha szükséges */
    while (cluster_offset + g_fat.bytes_per_cluster <= offset) {
        uint32_t next = fat_next_cluster(cluster);
        if (next >= CLUSTER_END_MARK || next < 2) {
            /* Lánc vége, de még nem értük el az offset-et: bővítés */
            uint32_t nc = fat_alloc_cluster();
            if (!nc) return 0;
            fat_set_entry(cluster, nc);
            uint8_t *zeros = (uint8_t *)kzalloc(g_fat.bytes_per_cluster);
            if (zeros) { fat_write_cluster(nc, zeros); kfree(zeros); }
            next = nc;
        }
        prev_cluster = cluster;
        cluster = next;
        cluster_offset += g_fat.bytes_per_cluster;
    }

    /* Írás cluster-onként */
    while (out_pos < size) {
        if (cluster < 2 || cluster >= CLUSTER_END_MARK) {
            /* Lánc vége: bővítjük */
            uint32_t nc = fat_alloc_cluster();
            if (!nc) break;
            fat_set_entry(prev_cluster ? prev_cluster : p->start_cluster, nc);
            /* Ha ez az első cluster az EOC volt, most frissítjük a start_cluster-t */
            cluster = nc;
            uint8_t *zeros = (uint8_t *)kzalloc(g_fat.bytes_per_cluster);
            if (zeros) { fat_write_cluster(nc, zeros); kfree(zeros); }
        }

        /* Olvassuk be az aktuális clustert, módosítjuk, visszaírjuk */
        uint8_t *cbuf = read_cluster(cluster);
        if (!cbuf) break;

        uint64_t in_cluster_offset = (out_pos == 0) ? (offset - cluster_offset) : 0;
        uint64_t copy_len = g_fat.bytes_per_cluster - in_cluster_offset;
        if (copy_len > size - out_pos) copy_len = size - out_pos;

        kmemcpy(cbuf + in_cluster_offset, buffer + out_pos, copy_len);
        fat_write_cluster(cluster, cbuf);
        kfree(cbuf);

        out_pos += copy_len;
        cluster_offset += g_fat.bytes_per_cluster;
        prev_cluster = cluster;
        cluster = fat_next_cluster(cluster);
    }

    /* Frissítjük a fájl méretét ha nőtt */
    uint64_t new_end = offset + out_pos;
    if (new_end > p->size) {
        p->size = (uint32_t)new_end;
        node->length = new_end;
        fat32_update_dirent(node, (uint32_t)new_end, (uint32_t)-1);
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

    n->read    = fat32_read;
    n->write   = is_dir ? NULL : fat32_write;
    n->readdir = fat32_readdir;
    n->finddir = fat32_finddir;
    n->create  = NULL;   /* later set for dirs */
    n->mkdir   = NULL;
    n->unlink  = NULL;
    p->dirent_sector = 0;
    p->dirent_off    = 0;
    p->children_cap  = 0;

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
        uint32_t base_lba = cluster_to_lba(cluster);  /* LBA a cluster első szektora */

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

            /* Dirent helye a lemezen */
            fat32_priv_t *cp = priv_of(child);
            if (cp) {
                cp->dirent_sector = base_lba + (i * 32) / SECTOR_SIZE;
                cp->dirent_off    = (i * 32) % SECTOR_SIZE;
            }

            /* Könyvtáraknál regisztráljuk a create/mkdir/unlink callbackeket */
            if (is_dir) {
                child->create = fat32_create_impl;
                child->mkdir  = fat32_mkdir_impl;
                child->unlink = fat32_unlink_impl;
            }

            /* Adjuk a kids listához */
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
    dp->children_cap = kids_cap;
}

/* --- FAT32 Write API: create, mkdir, unlink ------------------------------ */

/* 8.3 fájlnév előállítása (uppercase, kiterjesztés nélkül max 8 char). */
static void name_to_83(const char *name, uint8_t out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, o = 0;
    /* Alapnév */
    while (name[i] && name[i] != '.' && o < 8) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[o++] = (uint8_t)c;
    }
    /* Kiterjesztés keresése */
    const char *dot = name;
    while (*dot && *dot != '.') dot++;
    if (*dot == '.') {
        dot++;
        int eo = 8;
        while (*dot && eo < 11) {
            char c = *dot++;
            if (c >= 'a' && c <= 'z') c -= 32;
            out[eo++] = (uint8_t)c;
        }
    }
}

/* Szabad (0x00 vagy 0xE5) directory entry keresése a könyvtárban.
 * Visszaadja a szektor LBA-ját és a byte offset-et.
 * Ha nincs hely, 0-t ad vissza. */
static bool fat32_find_free_dirent(uint32_t dir_cluster,
                                   uint32_t *out_sector, uint32_t *out_off) {
    static uint8_t sec_buf[SECTOR_SIZE];
    uint32_t cluster = dir_cluster;

    while (cluster >= 2 && cluster < CLUSTER_END_MARK) {
        uint32_t base_lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < g_fat.sectors_per_cluster; s++) {
            if (read_blocks(base_lba + s, 1, sec_buf) != 1) continue;
            for (uint32_t e = 0; e < SECTOR_SIZE / 32; e++) {
                uint8_t first = sec_buf[e * 32];
                if (first == 0x00 || first == 0xE5) {
                    *out_sector = base_lba + s;
                    *out_off    = e * 32;
                    return true;
                }
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return false;
}

/* Hozzáadja az új gyermeket a könyvtár in-memory children tömbjéhez. */
static void dir_add_child(vfs_node_t *dir, vfs_node_t *child) {
    fat32_priv_t *dp = priv_of(dir);
    if (!dp) return;
    if (dp->child_count == dp->children_cap) {
        uint32_t nc = dp->children_cap ? dp->children_cap * 2 : 8;
        vfs_node_t **na = (vfs_node_t **)kmalloc(nc * sizeof(*na));
        if (!na) return;
        for (uint32_t k = 0; k < dp->child_count; k++) na[k] = dp->children[k];
        if (dp->children) kfree(dp->children);
        dp->children = na;
        dp->children_cap = nc;
    }
    dp->children[dp->child_count++] = child;
}

/* Fájl létrehozása egy könyvtárban (VFS create callback). */
static vfs_node_t *fat32_create_impl(vfs_node_t *dir, const char *name, uint32_t flags) {
    (void)flags;
    if (!dir || !name) return NULL;
    fat32_priv_t *dp = priv_of(dir);
    if (!dp || !dp->is_dir) return NULL;

    /* Allokálunk egy clustert az új fájlnak */
    uint32_t clust = fat_alloc_cluster();
    /* Üres cluster: size=0, cluster megadva */

    /* Szabad directory entry keresése */
    uint32_t ds, doff;
    if (!fat32_find_free_dirent(dp->start_cluster, &ds, &doff)) {
        if (clust) fat_set_entry(clust, 0);
        kprintf("[fat32] create: no free dirent for '%s'\n", name);
        return NULL;
    }

    /* 8.3 directory entry írása */
    static uint8_t sec_buf[SECTOR_SIZE];
    if (read_blocks(ds, 1, sec_buf) != 1) {
        if (clust) fat_set_entry(clust, 0);
        return NULL;
    }
    fat32_dir_entry_t *e = (fat32_dir_entry_t *)(sec_buf + doff);
    kmemset(e, 0, 32);
    name_to_83(name, e->name);
    e->attr        = ATTR_ARCHIVE;
    e->cluster_hi  = clust ? (uint16_t)(clust >> 16) : 0;
    e->cluster_lo  = clust ? (uint16_t)(clust & 0xFFFF) : 0;
    e->size        = 0;
    write_blocks(ds, 1, sec_buf);

    /* VFS node létrehozása */
    vfs_node_t *child = create_fat32_node(name, false, clust ? clust : 0, 0);
    if (!child) {
        if (clust) fat_set_entry(clust, 0);
        return NULL;
    }
    fat32_priv_t *cp = priv_of(child);
    if (cp) {
        cp->dirent_sector = ds;
        cp->dirent_off    = doff;
    }

    dir_add_child(dir, child);

    if (g_fat_node_count < FAT32_MAX_NODES)
        g_fat_nodes_pool[g_fat_node_count++] = child;

    return child;
}

/* Alkönyvtár létrehozása (VFS mkdir callback). */
static int fat32_mkdir_impl(vfs_node_t *dir, const char *name) {
    if (!dir || !name) return -1;
    fat32_priv_t *dp = priv_of(dir);
    if (!dp || !dp->is_dir) return -1;

    /* Cluster allokálás az új könyvtárnak */
    uint32_t clust = fat_alloc_cluster();
    if (!clust) return -1;

    /* "." és ".." bejegyzések írása az új clusterbe */
    uint8_t *dir_buf = (uint8_t *)kzalloc(g_fat.bytes_per_cluster);
    if (!dir_buf) { fat_set_entry(clust, 0); return -1; }

    fat32_dir_entry_t *dot = (fat32_dir_entry_t *)dir_buf;
    kmemset(dot, ' ', 11); dot->name[0] = '.';
    dot->attr = ATTR_DIRECTORY;
    dot->cluster_hi = (uint16_t)(clust >> 16);
    dot->cluster_lo = (uint16_t)(clust & 0xFFFF);

    fat32_dir_entry_t *dotdot = (fat32_dir_entry_t *)(dir_buf + 32);
    kmemset(dotdot->name, ' ', 11); dotdot->name[0] = '.'; dotdot->name[1] = '.';
    dotdot->attr = ATTR_DIRECTORY;
    dotdot->cluster_hi = (uint16_t)(dp->start_cluster >> 16);
    dotdot->cluster_lo = (uint16_t)(dp->start_cluster & 0xFFFF);

    fat_write_cluster(clust, dir_buf);
    kfree(dir_buf);

    /* Szabad dirent keresése a szülő könyvtárban */
    uint32_t ds, doff;
    if (!fat32_find_free_dirent(dp->start_cluster, &ds, &doff)) {
        fat_set_entry(clust, 0);
        return -1;
    }

    static uint8_t sec_buf[SECTOR_SIZE];
    if (read_blocks(ds, 1, sec_buf) != 1) { fat_set_entry(clust, 0); return -1; }
    fat32_dir_entry_t *e = (fat32_dir_entry_t *)(sec_buf + doff);
    kmemset(e, 0, 32);
    name_to_83(name, e->name);
    e->attr        = ATTR_DIRECTORY;
    e->cluster_hi  = (uint16_t)(clust >> 16);
    e->cluster_lo  = (uint16_t)(clust & 0xFFFF);
    e->size        = 0;
    write_blocks(ds, 1, sec_buf);

    /* VFS node létrehozása és hozzáadása */
    vfs_node_t *child = create_fat32_node(name, true, clust, 0);
    if (child) {
        fat32_priv_t *cp = priv_of(child);
        if (cp) { cp->dirent_sector = ds; cp->dirent_off = doff; }
        child->create = fat32_create_impl;
        child->mkdir  = fat32_mkdir_impl;
        child->unlink = fat32_unlink_impl;
        dir_add_child(dir, child);
        if (g_fat_node_count < FAT32_MAX_NODES)
            g_fat_nodes_pool[g_fat_node_count++] = child;
    }
    return 0;
}

/* Fájl törlése a könyvtárból (VFS unlink callback). */
static int fat32_unlink_impl(vfs_node_t *dir, const char *name) {
    if (!dir || !name) return -1;
    fat32_priv_t *dp = priv_of(dir);
    if (!dp || !dp->is_dir) return -1;

    /* Megkeressük a gyereket az in-memory listában */
    if (!dp->children_loaded) fat32_dir_load_children(dir);
    vfs_node_t *found = NULL;
    uint32_t found_idx = 0;
    for (uint32_t i = 0; i < dp->child_count; i++) {
        if (kstrcmp(dp->children[i]->name, name) == 0) {
            found = dp->children[i];
            found_idx = i;
            break;
        }
    }
    if (!found) return -1;

    fat32_priv_t *fp = priv_of(found);
    if (!fp) return -1;

    /* Directory entry törlése (0xE5 jelölő) */
    if (fp->dirent_sector != 0) {
        static uint8_t sec_buf[SECTOR_SIZE];
        if (read_blocks(fp->dirent_sector, 1, sec_buf) == 1) {
            sec_buf[fp->dirent_off] = 0xE5;
            write_blocks(fp->dirent_sector, 1, sec_buf);
        }
    }

    /* Cluster lánc felszabadítása */
    if (fp->start_cluster >= 2) {
        fat_free_chain(fp->start_cluster);
    }

    /* Kivétel az in-memory listából */
    for (uint32_t i = found_idx; i + 1 < dp->child_count; i++) {
        dp->children[i] = dp->children[i + 1];
    }
    dp->child_count--;

    return 0;
}

/* --- Init ---------------------------------------------------------------- */

static uint32_t find_fat32_partition(void) {
    /* Olvassuk be az LBA0-t. Ha boot szektor sig 0xAA55 + fs_type "FAT32",
     * akkor a teljes lemez egy FAT32 fájlrendszer (LBA 0-tól indul).
     * Egyébként MBR-t feltételezünk és keresünk egy FAT32 partíciót. */
    uint8_t buf[SECTOR_SIZE];
    if (read_blocks(0, 1, buf) != 1) return 0xFFFFFFFF;

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
    g_fat.bdev = block_get_first();
    if (!g_fat.bdev) {
        kprintf("[fat32] No block device available\n");
        return NULL;
    }
    kprintf("[fat32] using block device '%s'\n", g_fat.bdev->name);

    uint32_t part_lba = find_fat32_partition();
    if (part_lba == 0xFFFFFFFF) {
        kprintf("[fat32] No FAT32 partition found on disk\n");
        return NULL;
    }

    uint8_t bs_buf[SECTOR_SIZE];
    if (read_blocks(part_lba, 1, bs_buf) != 1) {
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
    if (root) {
        root->create = fat32_create_impl;
        root->mkdir  = fat32_mkdir_impl;
        root->unlink = fat32_unlink_impl;
    }
    return root;
}

/* Globális API wrapper-ek a fat32.h-ban deklarált függvényekhez */
vfs_node_t *fat32_create_file(vfs_node_t *dir, const char *name) {
    return fat32_create_impl(dir, name, 0);
}
int fat32_mkdir(vfs_node_t *dir, const char *name) {
    return fat32_mkdir_impl(dir, name);
}
int fat32_unlink(vfs_node_t *dir, const char *name) {
    return fat32_unlink_impl(dir, name);
}

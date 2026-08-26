// Estructuras binarias de NCA y NPDM necesarias para construir un forwarder.
// Portado de sphaira (https://github.com/ITotalJustice/sphaira), GPLv3,
// que a su vez las deriva de hacbrewpack y nxdumptool.
#pragma once

#include <switch.h>

namespace fwd {

inline constexpr u32 NCA3_MAGIC = 0x3341434E;
inline constexpr u32 NCA_SECTOR_SIZE = 0x200;
inline constexpr u32 NCA_SECTION_TOTAL = 0x4;

enum DistributionType
{
    DistributionType_System = 0x0,
    DistributionType_GameCard = 0x1,
};

enum ContentType
{
    ContentType_Program = 0x0,
    ContentType_Meta = 0x1,
    ContentType_Control = 0x2,
};

enum FileSystemType
{
    FileSystemType_RomFS = 0x0,
    FileSystemType_PFS0 = 0x1,
};

enum HashType
{
    HashType_Auto = 0x0,
    HashType_HierarchicalSha256 = 0x2,
    HashType_HierarchicalIntegrity = 0x3,
};

enum EncryptionType
{
    EncryptionType_Auto = 0x0,
    EncryptionType_None = 0x1,
    EncryptionType_AesXts = 0x2,
    EncryptionType_AesCtr = 0x3,
};

struct SectionTableEntry
{
    u32 media_start_offset;  // dividido por 0x200.
    u32 media_end_offset;    // dividido por 0x200.
    u8 _0x8[0x4];
    u8 _0xC[0x4];
};

struct LayerRegion
{
    u64 offset;
    u64 size;
};

struct HierarchicalSha256Data
{
    u8 master_hash[0x20];
    u32 block_size;
    u32 layer_count;
    LayerRegion hash_layer;
    LayerRegion pfs0_layer;
    LayerRegion unused_layers[3];
    u8 _0x78[0x80];
};
static_assert(sizeof(HierarchicalSha256Data) == 0xF8);

#pragma pack(push, 1)
struct HierarchicalIntegrityVerificationLevelInformation
{
    u64 logical_offset;
    u64 hash_data_size;
    u32 block_size;  // log2
    u32 _0x14;
};
#pragma pack(pop)

struct InfoLevelHash
{
    u32 max_layers;
    HierarchicalIntegrityVerificationLevelInformation levels[6];
    u8 signature_salt[0x20];
};

struct IntegrityMetaInfo
{
    u32 magic;  // IVFC
    u32 version;
    u32 master_hash_size;
    InfoLevelHash info_level_hash;
    u8 master_hash[0x20];
    u8 _0xE0[0x18];
};
static_assert(sizeof(IntegrityMetaInfo) == 0xF8);

struct BucketTreeHeader
{
    u32 magic;  // BKTR
    u32 version;
    u32 count;
    u8 _0xC[0x4];
};

struct PatchInfo
{
    u64 indirect_offset;
    u64 indirect_size;
    BucketTreeHeader indirect_header;
    u64 aes_ctr_offset;
    u64 aes_ctr_size;
    BucketTreeHeader aes_ctr_header;
};
static_assert(sizeof(PatchInfo) == 0x40);

struct CompressionInfo
{
    u64 table_offset;
    u64 table_size;
    BucketTreeHeader table_header;
    u8 _0x20[0x8];
};
static_assert(sizeof(CompressionInfo) == 0x28);

struct FsHeader
{
    u16 version;  // siempre 2.
    u8 fs_type;
    u8 hash_type;
    u8 encryption_type;
    u8 metadata_hash_type;
    u8 _0x6[0x2];

    union
    {
        HierarchicalSha256Data hierarchical_sha256_data;
        IntegrityMetaInfo integrity_meta_info;  // romfs
    } hash_data;

    PatchInfo patch_info;
    u64 section_ctr;
    u8 spares_info[0x30];
    CompressionInfo compression_info;
    u8 meta_data_hash_data_info[0x30];
    u8 reserved[0x30];
};
static_assert(sizeof(FsHeader) == 0x200);

struct SectionHeaderHash
{
    u8 sha256[0x20];
};

struct KeyArea
{
    u8 area[0x10];
};

struct NcaHeader
{
    u8 rsa_fixed_key[0x100];
    u8 rsa_npdm[0x100];
    u32 magic;
    u8 distribution_type;
    u8 content_type;
    u8 old_key_gen;
    u8 kaek_index;
    u64 size;
    u64 program_id;
    u32 context_id;
    u32 sdk_version;
    u8 key_gen;
    u8 sig_key_gen;
    u8 _0x222[0xE];
    FsRightsId rights_id;

    SectionTableEntry fs_table[NCA_SECTION_TOTAL];
    SectionHeaderHash fs_header_hash[NCA_SECTION_TOTAL];
    KeyArea key_area[NCA_SECTION_TOTAL];

    u8 _0x340[0xC0];

    FsHeader fs_header[NCA_SECTION_TOTAL];
};
static_assert(sizeof(NcaHeader) == 0xC00);

// --- NPDM ---

namespace npdm {

    struct Meta
    {
        u32 magic;  // "META"
        u32 signature_key_generation;
        u32 _0x8;
        u8 flags;
        u8 _0xD;
        u8 main_thread_priority;
        u8 main_thread_core_num;
        u32 _0x10;
        u32 sys_resource_size;
        u32 version;
        u32 main_thread_stack_size;
        char title_name[0x10];
        char product_code[0x10];
        u8 _0x40[0x30];
        u32 aci0_offset;
        u32 aci0_size;
        u32 acid_offset;
        u32 acid_size;
    };

    struct Acid
    {
        u8 rsa_sig[0x100];
        u8 rsa_pub[0x100];
        u32 magic;  // "ACID"
        u32 size;
        u8 version;
        u8 _0x209[0x1];
        u8 _0x20A[0x2];
        u32 flags;
        u64 program_id_min;
        u64 program_id_max;
        u32 fac_offset;
        u32 fac_size;
        u32 sac_offset;
        u32 sac_size;
        u32 kac_offset;
        u32 kac_size;
        u8 _0x238[0x8];
    };

    struct Aci0
    {
        u32 magic;  // "ACI0"
        u8 _0x4[0xC];
        u64 program_id;
        u8 _0x18[0x8];
        u32 fac_offset;
        u32 fac_size;
        u32 sac_offset;
        u32 sac_size;
        u32 kac_offset;
        u32 kac_size;
        u8 _0x38[0x8];
    };

}  // namespace npdm

// Registro que ns usa para asociar un título con su almacenamiento.
struct ContentStorageRecord
{
    NcmContentMetaKey key;
    u8 storage_id;  // NcmStorageId
    u8 padding[0x7];
};

}  // namespace fwd

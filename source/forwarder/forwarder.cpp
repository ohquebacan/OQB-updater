// Construye NCAs al vuelo e instala forwarders.
// Portado de sphaira (GPLv3) `source/owo.cpp`; la creación de romfs/pfs0/nca
// viene de hacbrewpack y la instalación de yati.
#include "forwarder.hpp"

#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility>

#include "forwarder/ns_ex.hpp"
#include "forwarder/nx_types.hpp"
#include "forwarder/result.hpp"

namespace fwd {
namespace {

    constexpr u32 IVFC_MAX_LEVEL = 6;
    constexpr u32 IVFC_HASH_BLOCK_SIZE = 0x4000;
    constexpr u32 PFS0_EXEFS_HASH_BLOCK_SIZE = 0x10000;
    constexpr u32 PFS0_META_HASH_BLOCK_SIZE = 0x1000;
    constexpr u32 PFS0_PADDING_SIZE = 0x200;
    constexpr u32 ROMFS_ENTRY_EMPTY = 0xFFFFFFFF;
    constexpr u32 ROMFS_FILEPARTITION_OFS = 0x200;

    constexpr const char HBL_MAIN_PATH[] = "romfs:/hbl/main";
    constexpr const char HBL_NPDM_PATH[] = "romfs:/hbl/main.npdm";

    template <typename F>
    struct ScopeGuard
    {
        explicit ScopeGuard(F f) : func(std::move(f)) {}
        ~ScopeGuard() { func(); }
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
        F func;
    };

#define FWD_CONCAT_(a, b) a##b
#define FWD_CONCAT(a, b) FWD_CONCAT_(a, b)
#define FWD_ON_SCOPE_EXIT(...) ScopeGuard FWD_CONCAT(fwd_scope_guard_, __LINE__){[&] { __VA_ARGS__; }}

#define FWD_TRY(expr)                       \
    {                                       \
        const Result fwd_rc_ = (expr);      \
        if (R_FAILED(fwd_rc_)) return fwd_rc_; \
    }

    constexpr std::size_t NacpLanguageEntryCount = 16;
    static_assert(sizeof(NacpLanguageEntry) * NacpLanguageEntryCount == 0x3000);

    // libnx renombró NacpStruct::lang a NacpStruct::lang_data.lang según la
    // versión; el bloque de idiomas siempre son los primeros 0x3000 bytes.
    auto NacpLanguageEntries(NacpStruct& nacp) -> std::span<NacpLanguageEntry, NacpLanguageEntryCount>
    {
        return std::span<NacpLanguageEntry, NacpLanguageEntryCount>{
            reinterpret_cast<NacpLanguageEntry*>(&nacp), NacpLanguageEntryCount};
    }

    // Buffer con semántica tipo stdio sobre std::vector.
    struct BufHelper
    {
        BufHelper() = default;
        explicit BufHelper(std::span<const u8> data) { write(data); }

        void write(const void* data, u64 size)
        {
            if (offset + size >= buf.size()) {
                buf.resize(offset + size);
            }
            std::memcpy(buf.data() + offset, data, size);
            offset += size;
        }

        void write(std::span<const u8> data) { write(data.data(), data.size()); }

        void seek(u64 where_to) { offset = where_to; }

        u64 tell() const { return offset; }

        std::vector<u8> buf;
        u64 offset{};
    };

    struct NcaEntry
    {
        NcaEntry(const BufHelper& b, NcmContentType _type) : data{b.buf}, type{(u8)_type}
        {
            sha256CalculateHash(hash, data.data(), data.size());
        }

        std::vector<u8> data;
        u8 type;
        u8 hash[SHA256_HASH_SIZE];
    };

    struct CnmtHeader
    {
        u64 title_id;
        u32 title_version;
        u8 meta_type;
        u8 _0xD;
        NcmContentMetaHeader meta_header;
        u8 install_type;
        u8 _0x17;
        u32 required_sys_version;
        u8 _0x1C[0x4];
    };
    static_assert(sizeof(CnmtHeader) == 0x20);

    struct NcmContentMetaData
    {
        NcmContentMetaHeader header;
        NcmApplicationMetaExtendedHeader extended;
        NcmContentInfo infos[3];
    };

    struct NcaMetaEntry
    {
        NcaMetaEntry(const BufHelper& b, NcmContentType type) : nca_entry{b, type} {}

        NcaEntry nca_entry;
        NcmContentMetaHeader content_meta_header{};
        NcmContentMetaKey content_meta_key{};
        ContentStorageRecord content_storage_record{};
        NcmContentMetaData content_meta_data{};
    };

    struct Pfs0Header
    {
        u32 magic;
        u32 total_files;
        u32 string_table_size;
        u32 padding;
    };

    struct Pfs0FileTable
    {
        u64 data_offset;
        u64 data_size;
        u32 name_offset;
        u32 padding;
    };

    struct FileEntry
    {
        std::string name;
        std::vector<u8> data;
    };

    using FileEntries = std::vector<FileEntry>;

    struct NpdmPatch
    {
        char title_name[0x10]{"Application"};
        char product_code[0x10]{};
        u64 tid;
    };

    struct NacpPatch
    {
        std::string name;
        std::string author;
        u64 tid;
    };

    typedef struct romfs_dirent_ctx
    {
        u32 entry_offset;
        struct romfs_dirent_ctx* parent;
        struct romfs_dirent_ctx* child;
        struct romfs_dirent_ctx* sibling;
        struct romfs_fent_ctx* file;
        struct romfs_dirent_ctx* next;
    } romfs_dirent_ctx_t;

    typedef struct romfs_fent_ctx
    {
        u32 entry_offset;
        u64 offset;
        u64 size;
        romfs_dirent_ctx_t* parent;
        struct romfs_fent_ctx* sibling;
        struct romfs_fent_ctx* next;
    } romfs_fent_ctx_t;

    typedef struct
    {
        romfs_fent_ctx_t* files;
        u64 num_dirs;
        u64 num_files;
        u64 dir_table_size;
        u64 file_table_size;
        u64 dir_hash_table_size;
        u64 file_hash_table_size;
        u64 file_partition_size;
    } romfs_ctx_t;

    auto write_padding(BufHelper& buf, u64 off, u64 block) -> u64
    {
        const u64 size = block - (off % block);
        if (size) {
            std::vector<u8> padding(size);
            buf.write(padding.data(), padding.size());
        }
        return size;
    }

    auto romfs_get_direntry(romfs_dir* directories, u32 offset) -> romfs_dir*
    {
        return (romfs_dir*)((u8*)directories + offset);
    }

    auto romfs_get_fentry(romfs_file* files, u32 offset) -> romfs_file*
    {
        return (romfs_file*)((u8*)files + offset);
    }

    auto calc_path_hash(u32 parent, const u8* path, u32 start, u32 path_len) -> u32
    {
        u32 hash = parent ^ 123456789;
        for (u32 i = 0; i < path_len; i++) {
            hash = (hash >> 5) | (hash << 27);
            hash ^= path[start + i];
        }
        return hash;
    }

    auto align_up(u32 offset, u32 alignment) -> u32
    {
        const u32 mask = ~(alignment - 1);
        return (offset + (alignment - 1)) & mask;
    }

    auto align_up64(u64 offset, u64 alignment) -> u64
    {
        const u64 mask = ~(u64)(alignment - 1);
        return (offset + (alignment - 1)) & mask;
    }

    auto romfs_get_hash_table_count(u32 num_entries) -> u32
    {
        if (num_entries < 3) {
            return 3;
        }
        else if (num_entries < 19) {
            return num_entries | 1;
        }

        u32 count = num_entries;
        while (count % 2 == 0 || count % 3 == 0 || count % 5 == 0 || count % 7 == 0 || count % 11 == 0 || count % 13 == 0 || count % 17 == 0) {
            count++;
        }
        return count;
    }

    void romfs_visit_dir(const FileEntries& entries, romfs_dirent_ctx_t* parent, romfs_ctx_t* romfs_ctx)
    {
        romfs_fent_ctx_t* child_file_tree = NULL;
        romfs_fent_ctx_t* cur_file = NULL;

        for (auto& e : entries) {
            cur_file = (romfs_fent_ctx_t*)calloc(1, sizeof(romfs_fent_ctx_t));

            romfs_ctx->num_files++;

            cur_file->parent = parent;
            cur_file->size = e.data.size();

            romfs_ctx->file_table_size += sizeof(romfs_file) + align_up(e.name.length() - 1, 4);

            if (child_file_tree == NULL) {
                cur_file->sibling = child_file_tree;
                child_file_tree = cur_file;
            }
            else {
                romfs_fent_ctx_t *child, *prev;
                prev = child_file_tree;
                child = child_file_tree->sibling;
                prev->sibling = cur_file;
                cur_file->sibling = child;
            }

            if (romfs_ctx->files == NULL) {
                cur_file->next = romfs_ctx->files;
                romfs_ctx->files = cur_file;
            }
            else {
                romfs_fent_ctx_t *child, *prev;
                prev = romfs_ctx->files;
                child = romfs_ctx->files->next;
                prev->next = cur_file;
                cur_file->next = child;
            }

            cur_file = NULL;
        }

        parent->child = NULL;
        parent->file = child_file_tree;
    }

    void build_romfs_into_file(const FileEntries& entries, BufHelper& buf)
    {
        auto root_ctx = (romfs_dirent_ctx_t*)calloc(1, sizeof(romfs_dirent_ctx_t));
        root_ctx->parent = root_ctx;

        romfs_ctx_t romfs_ctx{};

        romfs_ctx.dir_table_size = sizeof(romfs_dir);  // directorio raíz
        romfs_ctx.num_dirs = 1;

        romfs_visit_dir(entries, root_ctx, &romfs_ctx);
        const u32 dir_hash_table_entry_count = romfs_get_hash_table_count(romfs_ctx.num_dirs);
        const u32 file_hash_table_entry_count = romfs_get_hash_table_count(romfs_ctx.num_files);
        romfs_ctx.dir_hash_table_size = 4 * dir_hash_table_entry_count;
        romfs_ctx.file_hash_table_size = 4 * file_hash_table_entry_count;

        romfs_header header{};
        romfs_fent_ctx_t* cur_file{};
        romfs_dirent_ctx_t* cur_dir{};
        u32 entry_offset{};

        std::vector<u32> dir_hash_table(dir_hash_table_entry_count, ROMFS_ENTRY_EMPTY);
        std::vector<u32> file_hash_table(file_hash_table_entry_count, ROMFS_ENTRY_EMPTY);

        auto dir_table = (romfs_dir*)calloc(1, romfs_ctx.dir_table_size);
        auto file_table = (romfs_file*)calloc(1, romfs_ctx.file_table_size);

        // Offsets de los archivos.
        cur_file = romfs_ctx.files;
        entry_offset = 0;
        for (auto& e : entries) {
            romfs_ctx.file_partition_size = align_up64(romfs_ctx.file_partition_size, 0x10);
            cur_file->offset = romfs_ctx.file_partition_size;
            romfs_ctx.file_partition_size += cur_file->size;
            cur_file->entry_offset = entry_offset;
            entry_offset += sizeof(romfs_file) + align_up(e.name.length() - 1, 4);
            cur_file = cur_file->next;
        }

        root_ctx->entry_offset = 0x0;

        // Tabla de archivos.
        cur_file = romfs_ctx.files;
        for (auto& e : entries) {
            auto cur_entry = romfs_get_fentry(file_table, cur_file->entry_offset);
            cur_entry->parent = (cur_file->parent->entry_offset);
            cur_entry->sibling = (cur_file->sibling == NULL ? ROMFS_ENTRY_EMPTY : cur_file->sibling->entry_offset);
            cur_entry->dataOff = (cur_file->offset);
            cur_entry->dataSize = (cur_file->size);

            const u32 name_size = e.name.length() - 1;
            const u32 hash = calc_path_hash(cur_file->parent->entry_offset, (const u8*)e.name.c_str(), 1, name_size);
            cur_entry->nextHash = file_hash_table[hash % file_hash_table_entry_count];
            file_hash_table[hash % file_hash_table_entry_count] = (cur_file->entry_offset);

            cur_entry->nameLen = name_size;
            std::memcpy(cur_entry->name, e.name.c_str() + 1, name_size);

            cur_file = cur_file->next;
        }

        // Tabla de directorios.
        cur_dir = root_ctx;
        while (cur_dir != NULL) {
            auto cur_entry = romfs_get_direntry(dir_table, cur_dir->entry_offset);
            cur_entry->parent = cur_dir->parent->entry_offset;
            cur_entry->sibling = cur_dir->sibling == NULL ? ROMFS_ENTRY_EMPTY : cur_dir->sibling->entry_offset;
            cur_entry->childDir = cur_dir->child == NULL ? ROMFS_ENTRY_EMPTY : cur_dir->child->entry_offset;
            cur_entry->childFile = cur_dir->file == NULL ? ROMFS_ENTRY_EMPTY : cur_dir->file->entry_offset;

            const auto hash = calc_path_hash(0, 0, 0, 0);
            cur_entry->nextHash = dir_hash_table[hash % dir_hash_table_entry_count];
            dir_hash_table[hash % dir_hash_table_entry_count] = (cur_dir->entry_offset);

            cur_entry->nameLen = 0;

            auto temp = cur_dir;
            cur_dir = cur_dir->next;
            free(temp);
        }

        header.headerSize = sizeof(header);
        header.fileHashTableSize = romfs_ctx.file_hash_table_size;
        header.fileTableSize = romfs_ctx.file_table_size;
        header.dirHashTableSize = romfs_ctx.dir_hash_table_size;
        header.dirTableSize = romfs_ctx.dir_table_size;
        header.fileDataOff = ROMFS_FILEPARTITION_OFS;

        header.dirHashTableOff = align_up64(romfs_ctx.file_partition_size + ROMFS_FILEPARTITION_OFS, 4);
        header.dirTableOff = header.dirHashTableOff + romfs_ctx.dir_hash_table_size;
        header.fileHashTableOff = header.dirTableOff + romfs_ctx.dir_table_size;
        header.fileTableOff = header.fileHashTableOff + romfs_ctx.file_hash_table_size;

        buf.write(&header, sizeof(header));

        cur_file = romfs_ctx.files;
        for (auto& e : entries) {
            buf.seek(cur_file->offset + ROMFS_FILEPARTITION_OFS);
            buf.write(e.data.data(), e.data.size());

            auto temp = cur_file;
            cur_file = cur_file->next;
            free(temp);
        }

        buf.seek(header.dirHashTableOff);
        buf.write(dir_hash_table.data(), romfs_ctx.dir_hash_table_size);

        buf.write(dir_table, romfs_ctx.dir_table_size);
        free(dir_table);

        buf.write(file_hash_table.data(), romfs_ctx.file_hash_table_size);

        buf.write(file_table, romfs_ctx.file_table_size);
        free(file_table);
    }

    auto romfs_build(const FileEntries& entries, u64* out_size) -> std::vector<u8>
    {
        BufHelper buf;

        build_romfs_into_file(entries, buf);

        buf.seek(buf.buf.size());
        *out_size = buf.tell();
        write_padding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);

        return buf.buf;
    }

    auto npdm_patch_kc(std::vector<u8>& npdm_data, u32 off, u32 size, u32 bitmask, u32 value) -> bool
    {
        const u32 pattern = BIT(bitmask) - 1;
        const u32 mask = BIT(bitmask) | pattern;

        for (u32 i = 0; i < size; i += 4) {
            u32 cup;
            std::memcpy(&cup, npdm_data.data() + off + i, sizeof(cup));
            if ((cup & mask) == pattern) {
                cup = value | pattern;
                std::memcpy(npdm_data.data() + off + i, &cup, sizeof(cup));
                return true;
            }
        }

        return false;
    }

    // 3 núcleos: el cuarto está reservado para el sistema.
    constexpr auto npdm_kernel_flags() -> u32
    {
        const u32 lowest_priority = 59;
        const u32 highest_priority = 28;
        const u32 lowest_cpu = 0;
        const u32 highest_cpu = 2;
        const u32 descriptor = (((highest_cpu << 8) | lowest_cpu) << 6 | highest_priority) << 6 | lowest_priority;
        return descriptor << 4;
    }
    static_assert((npdm_kernel_flags() | 0x7) == 0x020073B7);

    auto patch_npdm(std::vector<u8>& npdm_data, const NpdmPatch& patch) -> bool
    {
        constexpr u8 ADDRESS_SPACE_SHIFT = 1;
        constexpr u8 ADDRESS_SPACE_MASK = 0x7 << ADDRESS_SPACE_SHIFT;
        constexpr u8 ADDRESS_SPACE_36BIT = 1;

        npdm::Meta meta{};
        npdm::Aci0 aci0{};
        npdm::Acid acid{};
        std::memcpy(&meta, npdm_data.data(), sizeof(meta));
        std::memcpy(&aci0, npdm_data.data() + meta.aci0_offset, sizeof(aci0));
        std::memcpy(&acid, npdm_data.data() + meta.acid_offset, sizeof(acid));

        std::memcpy(meta.title_name, &patch.title_name, sizeof(meta.title_name));
        std::memcpy(meta.product_code, &patch.product_code, sizeof(patch.product_code));
        meta.flags = (meta.flags & ~ADDRESS_SPACE_MASK) | (ADDRESS_SPACE_36BIT << ADDRESS_SPACE_SHIFT);
        aci0.program_id = patch.tid;
        acid.program_id_min = patch.tid;
        acid.program_id_max = patch.tid;

        const auto kernel_flags = npdm_kernel_flags();
        const auto aci0_core_patched = npdm_patch_kc(npdm_data, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 3, kernel_flags);
        const auto acid_core_patched = npdm_patch_kc(npdm_data, meta.acid_offset + acid.kac_offset, acid.kac_size, 3, kernel_flags);

        // Atmosphère 1.8.0 cambió el layout de MiscFlags para HOS 19.0.0: a
        // partir de ahí hay que activar el bit ForceDebug explícitamente.
        const auto enable_svc_debug = [] {
            u64 ver{};
            if (R_FAILED(splInitialize())) {
                return false;
            }
            FWD_ON_SCOPE_EXIT(splExit());

            const auto SplConfigItem_ExosphereVersion = (SplConfigItem)65000;
            if (R_FAILED(splGetConfig(SplConfigItem_ExosphereVersion, &ver))) {
                return false;
            }
            ver >>= 40;
            return ver >= MAKEHOSVERSION(1, 8, 0);
        }();

        if (enable_svc_debug) {
            npdm_patch_kc(npdm_data, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 16, BIT(19));
            npdm_patch_kc(npdm_data, meta.acid_offset + acid.kac_offset, acid.kac_size, 16, BIT(19));
        }

        std::memcpy(npdm_data.data(), &meta, sizeof(meta));
        std::memcpy(npdm_data.data() + meta.aci0_offset, &aci0, sizeof(aci0));
        std::memcpy(npdm_data.data() + meta.acid_offset, &acid, sizeof(acid));
        return aci0_core_patched && acid_core_patched;
    }

    void patch_nacp(NacpStruct& nacp, const NacpPatch& patch)
    {
        if (!patch.name.empty()) {
            for (auto& lang : NacpLanguageEntries(nacp)) {
                std::strncpy(lang.name, patch.name.c_str(), sizeof(lang.name) - 1);
            }
        }

        if (!patch.author.empty()) {
            for (auto& lang : NacpLanguageEntries(nacp)) {
                std::strncpy(lang.author, patch.author.c_str(), sizeof(lang.author) - 1);
            }
        }

        nacp.startup_user_account = 0x00;             // sin selector de perfil
        nacp.user_account_switch_lock = 0x00;         // permitir cambiar de cuenta
        nacp.add_on_content_registration_type = 0x01; // on demand
        nacp.screenshot = 0x0;                        // capturas permitidas
        nacp.video_capture = 0x2;                     // grabación automática
        nacp.logo_type = 0x2;                         // Nintendo
        nacp.logo_handling = 0x0;                     // auto
        nacp.data_loss_confirmation = 0x0;            // no usamos saves
        nacp.required_network_service_license_on_launch = 0x0;

        const char error_code[] = "OQB";
        static_assert(sizeof(error_code) <= 9);
        nacp.application_error_code_category = 0;  // en realidad es char[8]
        std::memcpy(&nacp.application_error_code_category, error_code, sizeof(error_code) - 1);

        nacp.presence_group_id = patch.tid;
        nacp.save_data_owner_id = patch.tid;
        nacp.pseudo_device_id_seed = patch.tid;
        nacp.add_on_content_base_id = patch.tid ^ 0x1000;
        for (auto& id : nacp.local_communication_id) {
            id = patch.tid;
        }

        nacp.play_log_policy = 0x0;
        nacp.play_log_query_capability = 0x0;

        // Sin creación de saves.
        nacp.user_account_save_data_size = 0x0;
        nacp.user_account_save_data_journal_size = 0x0;
        nacp.device_save_data_size = 0x0;
        nacp.device_save_data_journal_size = 0x0;
        nacp.user_account_save_data_size_max = 0x0;
        nacp.user_account_save_data_journal_size_max = 0x0;
        nacp.device_save_data_size_max = 0x0;
        nacp.device_save_data_journal_size_max = 0x0;
    }

    void add_file_entry(FileEntries& entries, const char* name, const void* data, u64 size)
    {
        FileEntry entry;
        entry.name = name;
        entry.data.resize(size);
        std::memcpy(entry.data.data(), data, size);
        entries.emplace_back(std::move(entry));
    }

    void add_file_entry(FileEntries& entries, const char* name, std::span<const u8> data)
    {
        add_file_entry(entries, name, data.data(), data.size());
    }

    auto build_ivfc_master_hash(std::span<const u8> level1) -> std::vector<u8>
    {
        std::vector<u8> hash(SHA256_HASH_SIZE);
        sha256CalculateHash(hash.data(), level1.data(), level1.size());
        return hash;
    }

    auto build_pfs0(const FileEntries& entries) -> std::vector<u8>
    {
        BufHelper buf;

        Pfs0Header header{};
        std::vector<Pfs0FileTable> file_table(entries.size());
        std::vector<char> string_table;

        u64 string_offset{};
        u64 data_offset{};

        for (u32 i = 0; i < entries.size(); i++) {
            file_table[i].data_offset = data_offset;
            file_table[i].data_size = entries[i].data.size();
            file_table[i].name_offset = string_offset;
            file_table[i].padding = 0;

            string_table.resize(string_offset + entries[i].name.length() + 1);
            std::memcpy(string_table.data() + string_offset, entries[i].name.c_str(), entries[i].name.length() + 1);

            data_offset += entries[i].data.size();
            string_offset += entries[i].name.length() + 1;
        }

        string_table.resize((string_table.size() + 0x1F) & ~0x1F);

        header.magic = 0x30534650;  // "PFS0"
        header.total_files = entries.size();
        header.string_table_size = string_table.size();
        header.padding = 0;

        buf.write(&header, sizeof(header));
        buf.write(file_table.data(), sizeof(Pfs0FileTable) * file_table.size());
        buf.write(string_table.data(), string_table.size());

        for (const auto& e : entries) {
            buf.write(e.data.data(), e.data.size());
        }

        return buf.buf;
    }

    auto build_pfs0_hash_table(const std::vector<u8>& pfs0, u32 block_size) -> std::vector<u8>
    {
        BufHelper buf;
        u8 hash[SHA256_HASH_SIZE];
        u32 read_size = block_size;

        for (u32 i = 0; i < pfs0.size(); i += read_size) {
            if (i + read_size >= pfs0.size()) {
                read_size = pfs0.size() - i;
            }
            sha256CalculateHash(hash, pfs0.data() + i, read_size);
            buf.write(hash, sizeof(hash));
        }

        return buf.buf;
    }

    auto build_pfs0_master_hash(const std::vector<u8>& pfs0_hash_table) -> std::vector<u8>
    {
        std::vector<u8> hash(SHA256_HASH_SIZE);
        sha256CalculateHash(hash.data(), pfs0_hash_table.data(), pfs0_hash_table.size());
        return hash;
    }

    void write_nca_padding(BufHelper& buf)
    {
        write_padding(buf, buf.tell(), 0x200);
    }

    void nca_encrypt_header(NcaHeader* header, std::span<const u8> key)
    {
        Aes128XtsContext ctx{};
        aes128XtsContextCreate(&ctx, key.data(), key.data() + 0x10, true);

        u8 sector{};
        for (u64 pos = 0; pos < 0xC00; pos += 0x200) {
            aes128XtsContextResetSector(&ctx, sector++, true);
            aes128XtsEncrypt(&ctx, (u8*)header + pos, (const u8*)header + pos, 0x200);
        }
    }

    void write_nca_section(NcaHeader& nca_header, u8 index, u64 start, u64 end)
    {
        auto& section = nca_header.fs_table[index];
        section.media_start_offset = start / 0x200;
        section.media_end_offset = end / 0x200;
        section._0x8[0] = 0x1;
    }

    void write_nca_fs_header_pfs0(NcaHeader& nca_header, u8 index, const std::vector<u8>& master_hash, u64 hash_table_size, u32 block_size)
    {
        auto& fs_header = nca_header.fs_header[index];
        fs_header.hash_type = HashType_HierarchicalSha256;
        fs_header.fs_type = FileSystemType_PFS0;
        fs_header.version = 0x2;
        fs_header.hash_data.hierarchical_sha256_data.layer_count = 0x2;
        fs_header.hash_data.hierarchical_sha256_data.block_size = block_size;
        fs_header.encryption_type = EncryptionType_None;
        fs_header.hash_data.hierarchical_sha256_data.hash_layer.size = hash_table_size;
        std::memcpy(fs_header.hash_data.hierarchical_sha256_data.master_hash, master_hash.data(), master_hash.size());
        sha256CalculateHash(&nca_header.fs_header_hash[index], &fs_header, sizeof(fs_header));
    }

    void write_nca_fs_header_romfs(NcaHeader& nca_header, u8 index)
    {
        auto& fs_header = nca_header.fs_header[index];
        fs_header.hash_type = HashType_HierarchicalIntegrity;
        fs_header.fs_type = FileSystemType_RomFS;
        fs_header.version = 0x2;
        fs_header.hash_data.integrity_meta_info.magic = 0x43465649;  // "IVFC"
        fs_header.hash_data.integrity_meta_info.version = 0x20000;
        fs_header.hash_data.integrity_meta_info.master_hash_size = SHA256_HASH_SIZE;
        fs_header.hash_data.integrity_meta_info.info_level_hash.max_layers = 0x7;
        fs_header.encryption_type = EncryptionType_None;
        fs_header.hash_data.integrity_meta_info.info_level_hash.levels[5].block_size = 0x0E;  // 0x4000
        sha256CalculateHash(&nca_header.fs_header_hash[index], &fs_header, sizeof(fs_header));
    }

    void write_nca_pfs0(NcaHeader& nca_header, u8 index, const FileEntries& entries, u32 block_size, BufHelper& buf)
    {
        const auto pfs0 = build_pfs0(entries);
        const auto pfs0_hash_table = build_pfs0_hash_table(pfs0, block_size);
        const auto pfs0_master_hash = build_pfs0_master_hash(pfs0_hash_table);

        buf.write(pfs0_hash_table.data(), pfs0_hash_table.size());
        const auto padding_size = write_padding(buf, pfs0_hash_table.size(), PFS0_PADDING_SIZE);

        nca_header.fs_header[index].hash_data.hierarchical_sha256_data.pfs0_layer.offset = pfs0_hash_table.size() + padding_size;
        nca_header.fs_header[index].hash_data.hierarchical_sha256_data.pfs0_layer.size = pfs0.size();

        buf.write(pfs0.data(), pfs0.size());
        write_nca_padding(buf);

        const auto section_start = index == 0 ? sizeof(nca_header) : nca_header.fs_table[index - 1].media_end_offset * 0x200;
        write_nca_section(nca_header, index, section_start, buf.tell());
        write_nca_fs_header_pfs0(nca_header, index, pfs0_master_hash, pfs0_hash_table.size(), block_size);
    }

    auto ivfc_create_level(const std::vector<u8>& src) -> std::vector<u8>
    {
        BufHelper buf;
        u8 hash[SHA256_HASH_SIZE];
        u64 read_size = IVFC_HASH_BLOCK_SIZE;

        for (u32 i = 0; i < src.size(); i += read_size) {
            if (i + read_size >= src.size()) {
                read_size = src.size() - i;
            }
            sha256CalculateHash(hash, src.data() + i, read_size);
            buf.write(hash, sizeof(hash));
        }

        write_padding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);

        return buf.buf;
    }

    void write_nca_romfs(NcaHeader& nca_header, u8 index, const FileEntries& entries, BufHelper& buf)
    {
        auto& fs_header = nca_header.fs_header[index];
        auto& meta_info = fs_header.hash_data.integrity_meta_info;
        auto& info_level_hash = meta_info.info_level_hash;

        std::vector<u8> ivfc[IVFC_MAX_LEVEL];

        ivfc[5] = romfs_build(entries, &info_level_hash.levels[5].hash_data_size);

        for (int b = 4; b >= 0; b--) {
            ivfc[b] = ivfc_create_level(ivfc[b + 1]);
            info_level_hash.levels[b].hash_data_size = ivfc[b].size();
            info_level_hash.levels[b].block_size = 0x0E;  // 0x4000
        }

        info_level_hash.levels[0].logical_offset = 0;
        for (int i = 1; i <= 5; i++) {
            info_level_hash.levels[i].logical_offset = info_level_hash.levels[i - 1].logical_offset + info_level_hash.levels[i - 1].hash_data_size;
        }

        for (const auto& iv : ivfc) {
            buf.write(iv.data(), iv.size());
        }

        write_nca_padding(buf);

        const auto ivfc_master_hash = build_ivfc_master_hash(ivfc[0]);
        std::memcpy(meta_info.master_hash, ivfc_master_hash.data(), sizeof(meta_info.master_hash));

        const auto section_start = index == 0 ? sizeof(nca_header) : nca_header.fs_table[index - 1].media_end_offset * 0x200;
        write_nca_section(nca_header, index, section_start, buf.tell());
        write_nca_fs_header_romfs(nca_header, index);
    }

    void write_nca_header_encrypted(NcaHeader& nca_header, u64 tid, const u8* header_key, ContentType type, BufHelper& buf)
    {
        nca_header.magic = NCA3_MAGIC;
        nca_header.distribution_type = DistributionType_System;
        nca_header.content_type = type;
        nca_header.program_id = tid;
        nca_header.sdk_version = 0x000C1100;
        nca_header.size = buf.tell();

        nca_encrypt_header(&nca_header, {header_key, 0x20});
        buf.seek(0);
        buf.write(&nca_header, sizeof(nca_header));
    }

    auto create_program_nca(u64 tid, const u8* header_key, const FileEntries& exefs, const FileEntries& romfs) -> NcaEntry
    {
        BufHelper buf;
        NcaHeader nca_header{};
        buf.write(&nca_header, sizeof(nca_header));

        write_nca_pfs0(nca_header, 0, exefs, PFS0_EXEFS_HASH_BLOCK_SIZE, buf);
        write_nca_romfs(nca_header, 1, romfs, buf);
        write_nca_header_encrypted(nca_header, tid, header_key, ContentType_Program, buf);

        return {buf, NcmContentType_Program};
    }

    auto create_control_nca(u64 tid, const u8* header_key, const FileEntries& romfs) -> NcaEntry
    {
        NcaHeader nca_header{};
        BufHelper buf;
        buf.write(&nca_header, sizeof(nca_header));

        write_nca_romfs(nca_header, 0, romfs, buf);
        write_nca_header_encrypted(nca_header, tid, header_key, ContentType_Control, buf);

        return {buf, NcmContentType_Control};
    }

    auto create_meta_nca(u64 tid, const u8* header_key, NcmStorageId storage_id, const std::vector<NcaEntry>& ncas) -> NcaMetaEntry
    {
        CnmtHeader cnmt_header{};
        NcmApplicationMetaExtendedHeader cnmt_extended{};
        NcmPackagedContentInfo packaged_content_info[2]{};
        BufHelper buf;
        u8 digest[SHA256_HASH_SIZE]{};

        cnmt_header.title_id = tid;
        cnmt_header.title_version = 0;
        cnmt_header.meta_type = NcmContentMetaType_Application;
        cnmt_header.install_type = NcmContentInstallType_Full;
        cnmt_header.required_sys_version = 0;
        cnmt_header.meta_header.extended_header_size = sizeof(cnmt_extended);
        cnmt_header.meta_header.content_count = 0x2;  // program + control
        cnmt_header.meta_header.content_meta_count = 0x1;
        cnmt_header.meta_header.attributes = 0x0;
        cnmt_header.meta_header.storage_id = storage_id;
        cnmt_extended.patch_id = cnmt_header.title_id | 0x800;

        for (u32 i = 0; i < ncas.size(); i++) {
            std::memcpy(packaged_content_info[i].hash, ncas[i].hash, sizeof(packaged_content_info[i].hash));
            std::memcpy(&packaged_content_info[i].info.content_id, ncas[i].hash, sizeof(packaged_content_info[i].info.content_id));
            packaged_content_info[i].info.content_type = ncas[i].type;
            ncmU64ToContentInfoSize(ncas[i].data.size(), &packaged_content_info[i].info);
        }

        BufHelper cnmt_buf;
        cnmt_buf.write(&cnmt_header, sizeof(cnmt_header));
        cnmt_buf.write(&cnmt_extended, sizeof(cnmt_extended));
        cnmt_buf.write(&packaged_content_info, sizeof(packaged_content_info));
        cnmt_buf.write(digest, sizeof(digest));

        FileEntries cnmt;
        char cnmt_name[34];
        std::snprintf(cnmt_name, sizeof(cnmt_name), "Application_%016lX.cnmt", tid);
        add_file_entry(cnmt, cnmt_name, cnmt_buf.buf.data(), cnmt_buf.buf.size());

        NcaHeader nca_header{};
        buf.write(&nca_header, sizeof(nca_header));
        write_nca_pfs0(nca_header, 0, cnmt, PFS0_META_HASH_BLOCK_SIZE, buf);
        write_nca_header_encrypted(nca_header, tid, header_key, ContentType_Meta, buf);

        NcaMetaEntry entry{buf, NcmContentType_Meta};

        entry.content_meta_header = cnmt_header.meta_header;
        entry.content_meta_header.content_count++;
        entry.content_meta_header.storage_id = 0;

        entry.content_meta_key.id = cnmt_header.title_id;
        entry.content_meta_key.version = cnmt_header.title_version;
        entry.content_meta_key.type = cnmt_header.meta_type;
        entry.content_meta_key.install_type = NcmContentInstallType_Full;
        std::memset(entry.content_meta_key.padding, 0, sizeof(entry.content_meta_key.padding));

        entry.content_storage_record.key = entry.content_meta_key;
        entry.content_storage_record.storage_id = storage_id;
        std::memset(entry.content_storage_record.padding, 0, sizeof(entry.content_storage_record.padding));

        entry.content_meta_data.header = entry.content_meta_header;
        entry.content_meta_data.extended = cnmt_extended;

        std::memcpy(&entry.content_meta_data.infos[0].content_id, entry.nca_entry.hash, sizeof(entry.content_meta_data.infos[0].content_id));
        entry.content_meta_data.infos[0].content_type = entry.nca_entry.type;
        entry.content_meta_data.infos[0].attr = 0;
        ncmU64ToContentInfoSize(cnmt_buf.buf.size(), &entry.content_meta_data.infos[0]);
        entry.content_meta_data.infos[0].id_offset = 0;

        entry.content_meta_data.infos[1] = packaged_content_info[0].info;
        entry.content_meta_data.infos[2] = packaged_content_info[1].info;

        return entry;
    }

    // header_key es la única clave que hace falta; se deriva de la consola,
    // así que no se necesita prod.keys en la SD.
    Result derive_header_key(u8* out /* 0x20 */)
    {
        constexpr u8 HEADER_KEK_SRC[0x10] = {
            0x1F, 0x12, 0x91, 0x3A, 0x4A, 0xCB, 0xF0, 0x0D, 0x4C, 0xDE, 0x3A, 0xF6, 0xD5, 0x23, 0x88, 0x2A};

        constexpr u8 HEADER_KEY_SRC[0x20] = {
            0x5A, 0x3E, 0xD8, 0x4F, 0xDE, 0xC0, 0xD8, 0x26, 0x31, 0xF7, 0xE2, 0x5D, 0x19, 0x7B, 0xF5, 0xD0,
            0x1C, 0x9B, 0x7B, 0xFA, 0xF6, 0x28, 0x18, 0x3D, 0x71, 0xF6, 0x4D, 0x73, 0xF1, 0x50, 0xB9, 0xD2};

        u8 header_kek[0x20];
        FWD_TRY(splCryptoGenerateAesKek(HEADER_KEK_SRC, 0, 0, header_kek));
        FWD_TRY(splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC, out));
        FWD_TRY(splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC + 0x10, out + 0x10));

        return 0;
    }

    Result read_romfs_file(const char* path, std::vector<u8>& out)
    {
        FILE* f = std::fopen(path, "rb");
        if (!f) return Result_HblMissing;
        FWD_ON_SCOPE_EXIT(std::fclose(f));

        std::fseek(f, 0, SEEK_END);
        const auto size = std::ftell(f);
        if (size <= 0) return Result_HblMissing;
        std::fseek(f, 0, SEEK_SET);

        out.resize(size);
        if (std::fread(out.data(), 1, size, f) != (size_t)size) {
            return Result_HblMissing;
        }

        return 0;
    }

    // Los argumentos completos son "<ruta del nro> <args extra>"; el título se
    // deriva de ahí, así que el mismo NRO siempre da el mismo tid.
    std::string build_full_args(const std::string& nro_path, const std::string& args)
    {
        if (args.empty()) {
            return nro_path;
        }
        return nro_path + ' ' + args;
    }

    u64 derive_tid(const std::string& nro_path, const std::string& full_args)
    {
        u64 hash_data[SHA256_HASH_SIZE / sizeof(u64)];
        const auto hash_path = nro_path + full_args;
        sha256CalculateHash(hash_data, hash_path.data(), hash_path.length());
        return 0x0500000000000000 | (hash_data[0] & 0x00FFFFFFFFFFF000);
    }

}  // namespace

Result install(Config& config, const ProgressFn& progress, NcmStorageId storage_id)
{
    // El total queda por encima del último paso reportado a propósito: el
    // paso final lo marca quien llama, cuando esta función ya retornó.
    constexpr int TOTAL_STEPS = 8;
    int step = 0;
    const auto advance = [&] {
        if (progress) progress(++step, TOTAL_STEPS);
    };

    if (config.nro_path.empty() || config.icon.empty()) {
        return Result_BadArgs;
    }

    advance();  // 1
    std::vector<u8> hbl_main, hbl_npdm;
    FWD_TRY(read_romfs_file(HBL_MAIN_PATH, hbl_main));
    FWD_TRY(read_romfs_file(HBL_NPDM_PATH, hbl_npdm));

    FWD_TRY(splCryptoInitialize());
    FWD_ON_SCOPE_EXIT(splCryptoExit());

    FWD_TRY(ncmInitialize());
    FWD_ON_SCOPE_EXIT(ncmExit());

    FWD_TRY(ns_ex::Initialize());
    FWD_ON_SCOPE_EXIT(ns_ex::Exit());

    advance();  // 2
    u8 header_key[0x20];
    FWD_TRY(derive_header_key(header_key));

    const auto full_args = build_full_args(config.nro_path, config.args);
    const auto tid = derive_tid(config.nro_path, full_args);

    // Si ya había un forwarder para este NRO hay que borrarlo ANTES de escribir
    // nada. Los content id son el sha256 del propio NCA, así que reinstalar el
    // mismo NRO produce ids idénticos: si se borrara después de registrarlos,
    // se estarían borrando los que se acaban de escribir y el menú HOME
    // mostraría el título con el ícono de nube, como si faltara descargarlo.
    // Es limpieza best-effort: si no existía, falla y no pasa nada.
    nsDeleteApplicationCompletely(tid);

    std::vector<NcaEntry> nca_entries;

    // NCA de programa: hbl + la ruta del NRO destino en su romfs.
    {
        advance();  // 3
        FileEntries exefs;
        add_file_entry(exefs, "main", hbl_main);
        add_file_entry(exefs, "main.npdm", hbl_npdm);

        FileEntries romfs;
        add_file_entry(romfs, "/nextArgv", full_args.data(), full_args.length());
        add_file_entry(romfs, "/nextNroPath", config.nro_path.data(), config.nro_path.length());

        NpdmPatch npdm_patch;
        npdm_patch.tid = tid;
        if (!patch_npdm(exefs[1].data, npdm_patch)) {
            return Result_NpdmPatchFailed;
        }

        nca_entries.emplace_back(create_program_nca(tid, header_key, exefs, romfs));
    }

    // NCA de control: nombre, autor e icono que se ven en el menú HOME.
    {
        advance();  // 4
        NacpPatch nacp_patch{};
        nacp_patch.tid = tid;
        nacp_patch.name = config.name;
        nacp_patch.author = config.author;
        patch_nacp(config.nacp, nacp_patch);

        FileEntries romfs;
        add_file_entry(romfs, "/control.nacp", &config.nacp, sizeof(config.nacp));
        add_file_entry(romfs, "/icon_AmericanEnglish.dat", config.icon);

        nca_entries.emplace_back(create_control_nca(tid, header_key, romfs));
    }

    NcmContentMetaKey content_meta_key;
    ContentStorageRecord content_storage_record;
    NcmContentMetaData content_meta_data;
    {
        advance();  // 5
        const auto meta_entry = create_meta_nca(tid, header_key, storage_id, nca_entries);

        nca_entries.emplace_back(meta_entry.nca_entry);
        content_meta_key = meta_entry.content_meta_key;
        content_storage_record = meta_entry.content_storage_record;
        content_meta_data = meta_entry.content_meta_data;
    }

    // Escribir los NCAs en el almacenamiento.
    {
        advance();  // 6
        NcmContentStorage cs;
        FWD_TRY(ncmOpenContentStorage(&cs, storage_id));
        FWD_ON_SCOPE_EXIT(ncmContentStorageClose(&cs));

        for (const auto& nca : nca_entries) {
            NcmContentId content_id;
            NcmPlaceHolderId placeholder_id;
            std::memcpy(&content_id, nca.hash, sizeof(content_id));
            FWD_TRY(ncmContentStorageGeneratePlaceHolderId(&cs, &placeholder_id));
            ncmContentStorageDeletePlaceHolder(&cs, &placeholder_id);
            FWD_TRY(ncmContentStorageCreatePlaceHolder(&cs, &content_id, &placeholder_id, nca.data.size()));
            FWD_TRY(ncmContentStorageWritePlaceHolder(&cs, &placeholder_id, 0, nca.data.data(), nca.data.size()));
            ncmContentStorageDelete(&cs, &content_id);
            FWD_TRY(ncmContentStorageRegister(&cs, &content_id, &placeholder_id));
        }
    }

    // Registrar el título en la base de datos de ncm.
    {
        advance();  // 7
        NcmContentMetaDatabase db;
        FWD_TRY(ncmOpenContentMetaDatabase(&db, storage_id));
        FWD_ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));

        FWD_TRY(ncmContentMetaDatabaseSet(&db, &content_meta_key, &content_meta_data, sizeof(content_meta_data)));
        FWD_TRY(ncmContentMetaDatabaseCommit(&db));
    }

    // Publicarlo en el menú HOME.
    {
        FWD_TRY(ns_ex::PushApplicationRecord(tid, &content_storage_record, 1));
        ns_ex::InvalidateApplicationControlCache(tid);
    }

    return 0;
}

Result remove(const std::string& nro_path, const std::string& args)
{
    if (nro_path.empty()) {
        return Result_BadArgs;
    }

    const auto tid = derive_tid(nro_path, build_full_args(nro_path, args));

    FWD_TRY(nsInitialize());
    FWD_ON_SCOPE_EXIT(nsExit());

    return nsDeleteApplicationCompletely(tid);
}

bool exists(const std::string& nro_path, const std::string& args)
{
    if (nro_path.empty()) {
        return false;
    }

    const auto tid = derive_tid(nro_path, build_full_args(nro_path, args));

    if (R_FAILED(nsInitialize())) {
        return false;
    }
    FWD_ON_SCOPE_EXIT(nsExit());

    s32 count = 0;
    if (R_FAILED(nsCountApplicationContentMeta(tid, &count))) {
        return false;
    }

    return count > 0;
}

std::string resultToString(Result rc)
{
    switch (rc) {
        case Result_BadArgs:
            return "argumentos inválidos";
        case Result_HblMissing:
            return "no se encontró el loader (romfs:/hbl)";
        case Result_NpdmPatchFailed:
            return "no se pudo parchear el NPDM";
        case Result_NroInvalid:
            return "el archivo no es un NRO válido";
        case Result_NroNoAssets:
            return "el NRO no tiene icono ni metadatos";
        default: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "error 0x%X (módulo %d, desc %d)", rc, R_MODULE(rc), R_DESCRIPTION(rc));
            return buf;
        }
    }
}

}  // namespace fwd

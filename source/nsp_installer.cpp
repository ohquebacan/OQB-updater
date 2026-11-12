/*
 * Instalador NSP para OQB Updater.
 * Basado en el metodo de instalacion de Goldleaf, Copyright 2018-2025 XorTroll.
 * Goldleaf y OQB Updater se distribuyen bajo GNU GPL v3.
 */
#include "nsp_installer.hpp"

#include <minizip/unzip.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "progress_event.hpp"
#include "forwarder/ns_ex.hpp"

namespace {
constexpr size_t BufferSize = 1024 * 1024;
constexpr const char* TempDir = "/config/aio-switch-updater/nsp-temp";
constexpr const char* LogPath = "/config/aio-switch-updater/nsp_install.log";

std::string fmt_contenidos(size_t n) { return "escribir " + std::to_string(n) + " NCA"; }

#pragma pack(push, 1)
struct Pfs0Header { u32 magic; u32 count; u32 stringsSize; u32 reserved; };
struct Pfs0Entry { u64 offset; u64 size; u32 nameOffset; u32 reserved; };
#pragma pack(pop)

struct PfsEntry {
    std::string name;
    u64 offset;
    u64 size;
};

struct PackagedContentMetaHeader {
    u64 id;
    u32 version;
    u8 type;
    u8 reserved0;
    u16 extended_header_size;
    u16 content_count;
    u16 content_meta_count;
    u8 attributes;
    u8 storage_id;
    u8 install_type;
    u8 reserved1;
    u32 required_download_system_version;
    u8 reserved2[4];
};
static_assert(sizeof(PackagedContentMetaHeader) == 0x20);

struct PackagedMeta {
    PackagedContentMetaHeader header{};
    std::vector<NcmPackagedContentInfo> contents;
    std::vector<u8> extended;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && lower(value.substr(value.size() - suffix.size())) == lower(suffix);
}

std::string contentId(const NcmContentId& id) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (u8 c : id.c) { out += hex[c >> 4]; out += hex[c & 15]; }
    return out;
}

bool readPfs0(FILE* file, std::vector<PfsEntry>& entries, u64& dataOffset) {
    Pfs0Header header{};
    if (std::fread(&header, sizeof(header), 1, file) != 1 || header.magic != 0x30534650 || !header.count || header.count > 1024) return false;
    std::vector<Pfs0Entry> raw(header.count);
    std::vector<char> strings(header.stringsSize + 1, 0);
    if (std::fread(raw.data(), sizeof(Pfs0Entry), raw.size(), file) != raw.size()) return false;
    if (header.stringsSize > 0x100000 || std::fread(strings.data(), 1, header.stringsSize, file) != header.stringsSize) return false;
    dataOffset = sizeof(header) + raw.size() * sizeof(Pfs0Entry) + header.stringsSize;
    for (const auto& item : raw) {
        if (item.nameOffset >= header.stringsSize) return false;
        entries.push_back({strings.data() + item.nameOffset, item.offset, item.size});
    }
    return true;
}

const PfsEntry* findEntry(const std::vector<PfsEntry>& entries, const std::string& name) {
    for (const auto& entry : entries) if (lower(entry.name) == lower(name)) return &entry;
    return nullptr;
}

bool readAt(FILE* file, u64 offset, void* buffer, size_t size) {
    return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 && std::fread(buffer, 1, size, file) == size;
}

bool saveEntry(FILE* source, const PfsEntry& entry, u64 dataOffset, const std::string& destination) {
    std::filesystem::create_directories(std::filesystem::path(destination).parent_path());
    std::unique_ptr<FILE, decltype(&std::fclose)> out(std::fopen(destination.c_str(), "wb"), std::fclose);
    if (!out) return false;
    std::vector<u8> buffer(BufferSize);
    u64 done = 0;
    while (done < entry.size) {
        const size_t chunk = static_cast<size_t>(std::min<u64>(buffer.size(), entry.size - done));
        if (!readAt(source, dataOffset + entry.offset + done, buffer.data(), chunk)) return false;
        if (std::fwrite(buffer.data(), 1, chunk, out.get()) != chunk) return false;
        done += chunk;
    }
    return true;
}

bool readMeta(const u8* data, size_t size, PackagedMeta& meta) {
    if (size < sizeof(PackagedContentMetaHeader)) return false;
    std::memcpy(&meta.header, data, sizeof(meta.header));
    const size_t contentsOffset = sizeof(meta.header) + meta.header.extended_header_size;
    if (contentsOffset > size || meta.header.content_count > (size - contentsOffset) / sizeof(NcmPackagedContentInfo)) return false;
    meta.extended.assign(data + sizeof(meta.header), data + contentsOffset);
    for (u32 i = 0; i < meta.header.content_count; i++) {
        NcmPackagedContentInfo info{};
        std::memcpy(&info, data + contentsOffset + i * sizeof(info), sizeof(info));
        if (info.info.content_type != NcmContentType_DeltaFragment) meta.contents.push_back(info);
    }
    return true;
}

std::vector<u8> makeInstallMeta(const PackagedMeta& packaged, const NcmContentInfo& self) {
    const auto type = static_cast<NcmContentMetaType>(packaged.header.type);
    size_t extendedSize = packaged.header.extended_header_size;
    if (type == NcmContentMetaType_SystemUpdate) extendedSize += sizeof(u32); // se corrige abajo con el valor real
    if (type == NcmContentMetaType_SystemUpdate && packaged.extended.size() >= sizeof(NcmSystemUpdateMetaExtendedHeader)) {
        extendedSize = packaged.header.extended_header_size;
        NcmSystemUpdateMetaExtendedHeader h{};
        std::memcpy(&h, packaged.extended.data(), sizeof(h));
        extendedSize += h.extended_data_size;
    } else if (type == NcmContentMetaType_Patch && packaged.extended.size() >= sizeof(NcmPatchMetaExtendedHeader)) {
        extendedSize = packaged.header.extended_header_size;
        NcmPatchMetaExtendedHeader h{};
        std::memcpy(&h, packaged.extended.data(), sizeof(h));
        extendedSize += h.extended_data_size;
    }
    std::vector<u8> out(sizeof(NcmContentMetaHeader) + extendedSize + (packaged.contents.size() + 1) * sizeof(NcmContentInfo), 0);
    NcmContentMetaHeader header{};
    header.extended_header_size = packaged.header.extended_header_size;
    header.content_count = static_cast<u16>(packaged.contents.size() + 1);
    header.content_meta_count = packaged.header.content_meta_count;
    header.attributes = packaged.header.attributes;
    header.storage_id = packaged.header.storage_id;
    std::memcpy(out.data(), &header, sizeof(header));
    std::memcpy(out.data() + sizeof(header), packaged.extended.data(), std::min(packaged.extended.size(), extendedSize));
    std::memcpy(out.data() + sizeof(header) + extendedSize, &self, sizeof(self));
    for (size_t i = 0; i < packaged.contents.size(); i++) {
        const auto& info = packaged.contents[i].info;
        std::memcpy(out.data() + sizeof(header) + extendedSize + (i + 1) * sizeof(info), &info, sizeof(info));
    }
    return out;
}

Result importTicket(FILE* file, const std::vector<PfsEntry>& entries, u64 dataOffset) {
    const PfsEntry* tik = nullptr;
    for (const auto& entry : entries) if (endsWith(entry.name, ".tik")) { tik = &entry; break; }
    if (!tik) return 0;
    const PfsEntry* cert = nullptr;
    for (const auto& entry : entries) if (endsWith(entry.name, ".cert")) { cert = &entry; break; }
    if (!cert || tik->size > 0x10000 || cert->size > 0x100000) return 0xBAD0B01;
    std::vector<u8> tikData(tik->size), certData(cert->size);
    if (!readAt(file, dataOffset + tik->offset, tikData.data(), tikData.size()) || !readAt(file, dataOffset + cert->offset, certData.data(), certData.size())) return 0xBAD0B02;
    Service es;
    Result rc = smGetService(&es, "es");
    if (R_SUCCEEDED(rc)) {
        rc = serviceDispatch(&es, 1,
            .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In, SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
            .buffers = { { tikData.data(), tikData.size() }, { certData.data(), certData.size() } });
        serviceClose(&es);
    }
    return rc;
}

Result updateRecord(NcmContentMetaDatabase& db, const PackagedMeta& packaged, const NcmContentInfo& metaInfo) {
    NcmContentMetaKey key{};
    key.id = packaged.header.id;
    key.version = packaged.header.version;
    key.type = packaged.header.type;
    key.install_type = NcmContentInstallType_Full;
    auto meta = makeInstallMeta(packaged, metaInfo);
    Result rc = ncmContentMetaDatabaseSet(&db, &key, meta.data(), meta.size());
    if (R_FAILED(rc)) return rc;
    rc = ncmContentMetaDatabaseCommit(&db);
    if (R_FAILED(rc)) return rc;

    const u64 baseId = key.type == NcmContentMetaType_Patch ? key.id - 0x800 : (key.type == NcmContentMetaType_AddOnContent ? key.id & ~0xFFFULL : key.id);
    fwd::ContentStorageRecord record{};
    record.key = key;
    record.storage_id = NcmStorageId_SdCard;
    rc = fwd::ns_ex::Initialize();
    if (R_FAILED(rc)) return rc;
    rc = fwd::ns_ex::PushApplicationRecord(baseId, &record, 1);
    if (R_SUCCEEDED(rc)) fwd::ns_ex::InvalidateApplicationControlCache(baseId);
    fwd::ns_ex::Exit();
    return rc;
}
}

namespace nspInstaller {
bool isNspUrl(const std::string& url) { return endsWith(url, ".nsp"); }

std::vector<std::string> findInArchive(const std::string& archivePath, const std::string& stripPrefix) {
    std::vector<std::string> paths;
    unzFile zip = unzOpen(archivePath.c_str());
    if (!zip) return paths;
    if (unzGoToFirstFile(zip) == UNZ_OK) {
        do {
            char name[0x301]{};
            unz_file_info info{};
            if (unzGetCurrentFileInfo(zip, &info, name, sizeof(name) - 1, nullptr, 0, nullptr, 0) != UNZ_OK) continue;
            std::string path(name);
            if (!stripPrefix.empty() && path.rfind(stripPrefix, 0) == 0) path.erase(0, stripPrefix.size());
            while (!path.empty() && path.front() == '/') path.erase(path.begin());
            if (endsWith(path, ".nsp")) paths.push_back("/" + path);
        } while (unzGoToNextFile(zip) == UNZ_OK);
    }
    unzClose(zip);
    return paths;
}

Result installAndDelete(const std::string& path) {
    ProgressEvent::instance().setStatusCode(0);
    ProgressEvent::instance().setTotalSteps(100);
    ProgressEvent::instance().setStep(0);
    Result rc = 0;
    /* La pantalla de progreso solo pasa a la siguiente etapa (o muestra el
       error) cuando la barra llega al final: se completa siempre, salga bien o
       mal. Antes una falla dejaba la pantalla colgada sin decir nada. */
    std::unique_ptr<FILE, decltype(&std::fclose)> log(std::fopen(LogPath, "w"), std::fclose);
    auto anotar = [&log](const std::string& linea, Result r) {
        if (!log) return;
        std::fprintf(log.get(), "%s: 0x%X (modulo %u, descripcion %u)\n", linea.c_str(), r, R_MODULE(r), R_DESCRIPTION(r));
        std::fflush(log.get());
    };
    struct StatusGuard {
        Result* rc;
        ~StatusGuard()
        {
            ProgressEvent::instance().setStatusCode(*rc);
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
        }
    } statusGuard{&rc};
    if (log) std::fprintf(log.get(), "NSP: %s\n", path.c_str());
#define NSP_FALLA(texto, valor) do { rc = (valor); anotar(texto, rc); return rc; } while (0)
#define NSP_FALLA_META(texto, valor) do { rc = (valor); ncmContentStorageDeletePlaceHolder(&storage, &metaPh); ncmContentStorageClose(&storage); anotar(texto, rc); return rc; } while (0)
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), std::fclose);
    if (!file) NSP_FALLA("abrir el NSP", 0xBAD0B04);
    std::vector<PfsEntry> entries;
    u64 dataOffset = 0;
    if (!readPfs0(file.get(), entries, dataOffset)) NSP_FALLA("leer la cabecera PFS0", 0xBAD0B05);

    const PfsEntry* cnmt = nullptr;
    for (const auto& entry : entries) if (endsWith(entry.name, ".cnmt.nca")) { cnmt = &entry; break; }
    if (!cnmt) NSP_FALLA("buscar el .cnmt.nca", 0xBAD0B06);
    // El servicio de instalacion (ncm) se abre aqui, como en forwarder.cpp.
    rc = ncmInitialize();
    if (R_FAILED(rc)) NSP_FALLA("iniciar ncm", rc);
    struct NcmGuard { ~NcmGuard() { ncmExit(); } } ncmGuard;
    NcmContentStorage storage;
    rc = ncmOpenContentStorage(&storage, NcmStorageId_SdCard);
    if (R_FAILED(rc)) NSP_FALLA("abrir el almacenamiento de la SD", rc);

    /* El .cnmt.nca se lee desde un placeholder de ncm, como Goldleaf: un
       archivo suelto en la SD no se puede abrir como ContentMeta. */
    NcmContentId metaId{};
    {
        const std::string idHex = cnmt->name.substr(0, cnmt->name.find('.'));
        auto hex = [](char c) -> u8 { return c <= '9' ? c - '0' : (std::tolower(c) - 'a' + 10); };
        for (size_t i = 0; i < 16 && i * 2 + 1 < idHex.size(); i++)
            metaId.c[i] = (hex(idHex[i * 2]) << 4) | hex(idHex[i * 2 + 1]);
    }
    NcmPlaceHolderId metaPh{};
    rc = ncmContentStorageGeneratePlaceHolderId(&storage, &metaPh);
    if (R_SUCCEEDED(rc)) {
        ncmContentStorageDeletePlaceHolder(&storage, &metaPh);
        rc = ncmContentStorageCreatePlaceHolder(&storage, &metaId, &metaPh, cnmt->size);
    }
    if (R_SUCCEEDED(rc)) {
        std::vector<u8> buf(static_cast<size_t>(cnmt->size));
        if (!readAt(file.get(), dataOffset + cnmt->offset, buf.data(), buf.size())) rc = 0xBAD0B07;
        else rc = ncmContentStorageWritePlaceHolder(&storage, &metaPh, 0, buf.data(), buf.size());
    }
    char phPath[FS_MAX_PATH] = {};
    if (R_SUCCEEDED(rc)) rc = ncmContentStorageGetPlaceHolderPath(&storage, phPath, sizeof(phPath), &metaPh);
    if (R_FAILED(rc)) {
        ncmContentStorageDeletePlaceHolder(&storage, &metaPh);
        ncmContentStorageClose(&storage);
        NSP_FALLA("copiar el .cnmt.nca a un placeholder", rc);
    }
    const std::string contentPath = phPath;
    anotar("cnmt en " + contentPath, 0);
    FsFileSystem metaFs;
    rc = fsOpenFileSystemWithId(&metaFs, 0, FsFileSystemType_ContentMeta, contentPath.c_str(), FsContentAttributes_All);
    if (R_FAILED(rc)) NSP_FALLA_META("abrir el .cnmt.nca (" + contentPath + ")", rc);
    FsDir dir;
    rc = fsFsOpenDirectory(&metaFs, "/", FsDirOpenMode_ReadFiles, &dir);
    if (R_FAILED(rc)) { fsFsClose(&metaFs); NSP_FALLA_META("listar el .cnmt.nca", rc); }
    FsDirectoryEntry dirEntry{};
    s64 readCount = 0;
    std::string cnmtName;
    while (R_SUCCEEDED(fsDirRead(&dir, &readCount, 1, &dirEntry)) && readCount == 1) {
        if (endsWith(dirEntry.name, ".cnmt")) { cnmtName = dirEntry.name; break; }
    }
    fsDirClose(&dir);
    if (cnmtName.empty()) { fsFsClose(&metaFs); NSP_FALLA_META("buscar el .cnmt dentro", 0xBAD0B08); }
    FsFile metaFile;
    rc = fsFsOpenFile(&metaFs, ("/" + cnmtName).c_str(), FsOpenMode_Read, &metaFile);
    if (R_FAILED(rc)) { fsFsClose(&metaFs); NSP_FALLA_META("abrir el .cnmt", rc); }
    s64 metaSize = 0;
    rc = fsFileGetSize(&metaFile, &metaSize);
    std::vector<u8> metaData(metaSize > 0 && metaSize < 0x1000000 ? metaSize : 0);
    u64 actual = 0;
    if (R_SUCCEEDED(rc) && !metaData.empty()) rc = fsFileRead(&metaFile, 0, metaData.data(), metaData.size(), FsReadOption_None, &actual);
    fsFileClose(&metaFile);
    fsFsClose(&metaFs);
    PackagedMeta packaged;
    if (R_FAILED(rc) || actual != metaData.size() || !readMeta(metaData.data(), metaData.size(), packaged)) NSP_FALLA_META("leer el .cnmt", R_FAILED(rc) ? rc : 0xBAD0B09);

    ncmContentStorageDeletePlaceHolder(&storage, &metaPh);
    NcmContentMetaDatabase db;
    rc = ncmOpenContentMetaDatabase(&db, NcmStorageId_SdCard);
    if (R_FAILED(rc)) { ncmContentStorageClose(&storage); NSP_FALLA("abrir la base de datos de la SD", rc); }

    NcmContentInfo metaInfo{};
    const std::string idName = cnmt->name.substr(0, cnmt->name.find('.'));
    for (size_t i = 0; i < 16 && i * 2 < idName.size(); i++) {
        auto hex = [](char c) -> u8 { return c <= '9' ? c - '0' : (std::tolower(c) - 'a' + 10); };
        metaInfo.content_id.c[i] = (hex(idName[i * 2]) << 4) | hex(idName[i * 2 + 1]);
    }
    metaInfo.content_type = NcmContentType_Meta;
    ncmU64ToContentInfoSize(cnmt->size, &metaInfo);
    std::vector<NcmContentInfo> contents;
    contents.push_back(metaInfo);
    for (const auto& item : packaged.contents) contents.push_back(item.info);

    rc = importTicket(file.get(), entries, dataOffset);
    anotar("ticket", rc);
    u64 total = 0;
    if (R_SUCCEEDED(rc)) for (const auto& info : contents) {
        const std::string name = contentId(info.content_id) + (info.content_type == NcmContentType_Meta ? ".cnmt.nca" : ".nca");
        if (const PfsEntry* entry = findEntry(entries, name)) total += entry->size;
    }
    u64 completed = 0;
    std::vector<u8> buffer(BufferSize);
    for (const auto& info : contents) {
        if (R_FAILED(rc)) break;
        const std::string fileName = contentId(info.content_id) + (info.content_type == NcmContentType_Meta ? ".cnmt.nca" : ".nca");
        const PfsEntry* entry = findEntry(entries, fileName);
        if (!entry) { rc = 0xBAD0B0A; anotar("falta " + fileName + " en el NSP", rc); break; }
        NcmPlaceHolderId placeholder{};
        rc = ncmContentStorageGeneratePlaceHolderId(&storage, &placeholder);
        if (R_FAILED(rc)) break;
        ncmContentStorageDeletePlaceHolder(&storage, &placeholder);
        rc = ncmContentStorageCreatePlaceHolder(&storage, &info.content_id, &placeholder, entry->size);
        u64 offset = 0;
        while (R_SUCCEEDED(rc) && offset < entry->size) {
            const size_t chunk = static_cast<size_t>(std::min<u64>(buffer.size(), entry->size - offset));
            if (!readAt(file.get(), dataOffset + entry->offset + offset, buffer.data(), chunk)) { rc = 0xBAD0B0B; break; }
            rc = ncmContentStorageWritePlaceHolder(&storage, &placeholder, offset, buffer.data(), chunk);
            offset += chunk;
            completed += chunk;
            if (total) ProgressEvent::instance().setStep(static_cast<int>(completed * 100 / total));
        }
        if (R_SUCCEEDED(rc)) {
            // Al reinstalar o actualizar, el mismo NCA ya puede estar registrado.
            ncmContentStorageDelete(&storage, &info.content_id);
            rc = ncmContentStorageRegister(&storage, &info.content_id, &placeholder);
        }
        ncmContentStorageDeletePlaceHolder(&storage, &placeholder);
        if (R_FAILED(rc)) anotar("NCA " + fileName, rc);
    }
    anotar(fmt_contenidos(contents.size()), rc);
    if (R_SUCCEEDED(rc)) {
        rc = updateRecord(db, packaged, metaInfo);
        anotar("registro en el menu HOME", rc);
    }
    ncmContentMetaDatabaseClose(&db);
    ncmContentStorageClose(&storage);
    std::error_code ec;
    std::filesystem::remove_all(TempDir, ec);
    if (R_SUCCEEDED(rc)) {
        std::filesystem::remove(path, ec);
        ProgressEvent::instance().setStep(100);
    }
    anotar("fin", rc);
    return rc;
#undef NSP_FALLA
#undef NSP_FALLA_META
}
}

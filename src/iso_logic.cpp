#include "iso_logic.h"
#include <fstream>
#include <cstring>
#include <algorithm>

#pragma pack(push, 1)
struct DirectoryRecord {
    uint8_t length;
    uint8_t ext_attr_length;
    uint32_t lba_le;
    uint32_t lba_be;
    uint32_t size_le;
    uint32_t size_be;
    uint8_t datetime[7];
    uint8_t flags;
    uint8_t unit_size;
    uint8_t gap_size;
    uint16_t vol_seq_le;
    uint16_t vol_seq_be;
    uint8_t name_len;
};

struct SfoHeader {
    char magic[4]; 
    uint32_t version;
    uint32_t key_table_start;
    uint32_t data_table_start;
    uint32_t index_table_entries;
};

struct SfoEntry {
    uint16_t key_offset;
    uint8_t data_fmt;
    uint8_t data_type;
    uint32_t data_len;
    uint32_t data_max_len;
    uint32_t data_offset;
};
#pragma pack(pop)

std::string IsoProcessor::detect_format(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "UNKNOWN";
    char magic[4];
    f.read(magic, 4);
    if (std::memcmp(magic, "CISO", 4) == 0) return "CSO";
    
    f.seekg(32768 + 1);
    char iso_magic[5];
    f.read(iso_magic, 5);
    if (std::memcmp(iso_magic, "CD001", 5) == 0) return "ISO";
    return "UNKNOWN";
}

std::string IsoProcessor::get_region(const std::string& id) {
    if (id.empty() || id == "UNKNOWN") return "UNKNOWN";
    std::string gid = id;
    for (auto & c: gid) c = toupper(c);

    if (gid.find("UCES") != std::string::npos || gid.find("ULES") != std::string::npos) return "EUROPA";
    if (gid.find("UCUS") != std::string::npos || gid.find("ULUS") != std::string::npos) return "USA";
    if (gid.find("UCJS") != std::string::npos || gid.find("ULJS") != std::string::npos) return "JAPÓN";
    if (gid.find("UCAS") != std::string::npos || gid.find("ULAS") != std::string::npos) return "ASIA";
    
    if (gid.size() >= 4) {
        char r = gid[2];
        if (r == 'E' || r == 'P') return "EUROPA";
        if (r == 'U') return "USA";
        if (r == 'J') return "JAPÓN";
        if (r == 'A') return "ASIA";
    }
    return "DESCONOCIDO";
}

GameMetadata IsoProcessor::get_metadata(const std::string& path) {
    std::string fmt = detect_format(path);
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    uint64_t size = f.tellg();
    f.seekg(0);

    GameMetadata md;
    md.format = fmt;
    md.size = (size == (uint64_t)-1) ? 0 : size;
    md.id = "UNKNOWN";
    md.title = "UNKNOWN";
    
    size_t last_slash = path.find_last_of("\\/");
    std::string filename = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
    size_t last_dot = filename.find_last_of(".");
    md.title = (last_dot == std::string::npos) ? filename : filename.substr(0, last_dot);

    if (fmt == "ISO") {
        f.seekg(32768 + 156);
        DirectoryRecord root;
        f.read((char*)&root, sizeof(root));
        
        uint32_t root_lba = root.lba_le;
        uint32_t root_size = root.size_le;

        auto find_in_dir = [&](uint32_t dir_lba, uint32_t dir_size, std::string target) -> DirectoryRecord {
            std::vector<char> buffer(dir_size);
            f.seekg((uint64_t)dir_lba * 2048);
            f.read(buffer.data(), dir_size);
            
            uint32_t ptr = 0;
            while (ptr < dir_size) {
                if (ptr + sizeof(DirectoryRecord) > dir_size) break;
                DirectoryRecord* rec = (DirectoryRecord*)&buffer[ptr];
                
                if (rec->length == 0) {
                    ptr = ((ptr / 2048) + 1) * 2048;
                    continue;
                }
                
                std::string name;
                if (rec->name_len > 0 && (ptr + sizeof(DirectoryRecord) + rec->name_len <= dir_size)) {
                    name.assign(&buffer[ptr + sizeof(DirectoryRecord)], rec->name_len);
                    size_t semi = name.find(';');
                    if (semi != std::string::npos) name = name.substr(0, semi);
                }
                
                if (name == target) return *rec;
                ptr += rec->length;
            }
            DirectoryRecord empty = {0};
            return empty;
        };

        DirectoryRecord p_game = find_in_dir(root_lba, root_size, "PSP_GAME");
        if (p_game.length > 0) {
            DirectoryRecord sfo_rec = find_in_dir(p_game.lba_le, p_game.size_le, "PARAM.SFO");
            DirectoryRecord icon_rec = find_in_dir(p_game.lba_le, p_game.size_le, "ICON0.PNG");
            
            if (sfo_rec.length > 0) {
                std::vector<char> sfo_data(sfo_rec.size_le);
                f.seekg((uint64_t)sfo_rec.lba_le * 2048);
                f.read(sfo_data.data(), sfo_rec.size_le);
                
                if (sfo_data.size() >= sizeof(SfoHeader)) {
                    SfoHeader* hdr = (SfoHeader*)sfo_data.data();
                    if (std::memcmp(hdr->magic, "\x00PSF", 4) == 0) {
                        SfoEntry* entries = (SfoEntry*)(sfo_data.data() + sizeof(SfoHeader));
                        for (uint32_t i = 0; i < hdr->index_table_entries; ++i) {
                            if (hdr->key_table_start + entries[i].key_offset >= sfo_data.size()) continue;
                            const char* s_key = &sfo_data[hdr->key_table_start + entries[i].key_offset];
                            if (std::strcmp(s_key, "TITLE") == 0) {
                                if (hdr->data_table_start + entries[i].data_offset < sfo_data.size()) {
                                    md.title.assign(&sfo_data[hdr->data_table_start + entries[i].data_offset], entries[i].data_len);
                                    while (!md.title.empty() && md.title.back() == '\0') md.title.pop_back();
                                }
                            } else if (std::strcmp(s_key, "DISC_ID") == 0) {
                                if (hdr->data_table_start + entries[i].data_offset < sfo_data.size()) {
                                    md.id.assign(&sfo_data[hdr->data_table_start + entries[i].data_offset], entries[i].data_len);
                                    while (!md.id.empty() && md.id.back() == '\0') md.id.pop_back();
                                }
                            }
                        }
                    }
                }
            }
            
            if (icon_rec.length > 0 && icon_rec.size_le < 5000000) {
                md.icon_data.resize(icon_rec.size_le);
                f.seekg((uint64_t)icon_rec.lba_le * 2048);
                f.read((char*)md.icon_data.data(), icon_rec.size_le);
            }
        }
    }
    
    md.region = get_region(md.id);
    return md;
}

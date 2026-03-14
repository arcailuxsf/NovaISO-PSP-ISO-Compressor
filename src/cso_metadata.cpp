#include "cso_metadata.h"
#include <fstream>
#include <vector>
#include <zlib.h>
#include <cstring>

#pragma pack(push, 1)
struct CisoHeader {
    char magic[4];
    uint32_t header_size;
    uint64_t total_bytes;
    uint32_t block_size;
    uint8_t version;
    uint8_t align;
    uint8_t reserved[2];
};

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

class CsoSectorReader {
    std::ifstream f;
    CisoHeader hdr;
    std::vector<uint32_t> index;
public:
    bool open(const std::string& path) {
        f.open(path, std::ios::binary);
        if (!f) return false;
        f.read((char*)&hdr, 0x18);
        if (std::memcmp(hdr.magic, "CISO", 4) != 0) return false;
        
        uint32_t total_blocks = (uint32_t)((hdr.total_bytes + hdr.block_size - 1) / hdr.block_size);
        index.resize(total_blocks + 1);
        f.read((char*)index.data(), (total_blocks + 1) * 4);
        return true;
    }

    uint64_t get_total_size() { return hdr.total_bytes; }

    bool read_sector(uint32_t lba, void* buf) {
        if (lba >= index.size() - 1) return false;
        
        uint32_t idx = index[lba];
        bool is_raw = idx & 0x80000000;
        uint64_t pos = (uint64_t)(idx & 0x7FFFFFFF) << hdr.align;
        uint64_t next_pos = (uint64_t)(index[lba + 1] & 0x7FFFFFFF) << hdr.align;
        uint32_t chunk_size = (uint32_t)(next_pos - pos);

        f.seekg(pos);
        if (is_raw) {
            f.read((char*)buf, hdr.block_size);
        } else {
            std::vector<uint8_t> c_buf(chunk_size);
            f.read((char*)c_buf.data(), chunk_size);
            
            z_stream strm = {0};
            if (inflateInit2(&strm, -15) != Z_OK) return false;
            strm.next_in = (Bytef*)c_buf.data();
            strm.avail_in = chunk_size;
            strm.next_out = (Bytef*)buf;
            strm.avail_out = hdr.block_size;
            inflate(&strm, Z_FINISH);
            inflateEnd(&strm);
        }
        return true;
    }
};

GameMetadata CsoMetadataProcessor::get_metadata(const std::string& path) {
    CsoSectorReader reader;
    GameMetadata md;
    md.format = "CSO";
    md.title = "UNKNOWN";
    md.id = "UNKNOWN";
    md.region = "DESCONOCIDO";
    md.size = 0;

    if (!reader.open(path)) return md;
    md.size = reader.get_total_size();
    
    size_t last_slash = path.find_last_of("\\/");
    std::string filename = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
    size_t last_dot = filename.find_last_of(".");
    md.title = (last_dot == std::string::npos) ? filename : filename.substr(0, last_dot);

    // Get metadata from CSO (simulating ISO traversal using reader.read_sector)
    char buffer[2048];
    if (!reader.read_sector(16, buffer)) return md; // Volume Descriptor

    // Simplified Root Directory Search (LBA 16, pos 156 has root record)
    DirectoryRecord* root = (DirectoryRecord*)&buffer[156];
    uint32_t root_lba = root->lba_le;
    uint32_t root_size = root->size_le;

    auto find_in_dir = [&](uint32_t dir_lba, uint32_t dir_size, std::string target) -> DirectoryRecord {
        uint32_t sectors = (dir_size + 2047) / 2048;
        for (uint32_t s = 0; s < sectors; ++s) {
            char s_buf[2048];
            if (!reader.read_sector(dir_lba + s, s_buf)) break;
            
            uint32_t ptr = 0;
            while (ptr < 2048 && ptr < dir_size - (s * 2048)) {
                DirectoryRecord* rec = (DirectoryRecord*)&s_buf[ptr];
                if (rec->length == 0) break;
                
                std::string name;
                if (rec->name_len > 0) {
                    name.assign(&s_buf[ptr + sizeof(DirectoryRecord)], rec->name_len);
                    size_t semi = name.find(';');
                    if (semi != std::string::npos) name = name.substr(0, semi);
                }
                
                if (name == target) return *rec;
                ptr += rec->length;
            }
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
            uint32_t sfo_sectors = (sfo_rec.size_le + 2047) / 2048;
            for (uint32_t i = 0; i < sfo_sectors; ++i) {
                char s_buf[2048];
                if (reader.read_sector(sfo_rec.lba_le + i, s_buf)) {
                    uint32_t to_copy = std::min((uint32_t)2048, (uint32_t)(sfo_rec.size_le - (i * 2048)));
                    std::memcpy(sfo_data.data() + (i * 2048), s_buf, to_copy);
                }
            }
            
            if (sfo_data.size() >= sizeof(SfoHeader)) {
                SfoHeader* hdr = (SfoHeader*)sfo_data.data();
                if (std::memcmp(hdr->magic, "\x00PSF", 4) == 0) {
                    SfoEntry* entries = (SfoEntry*)(sfo_data.data() + sizeof(SfoHeader));
                    for (uint32_t i = 0; i < hdr->index_table_entries; ++i) {
                        const char* key = &sfo_data[hdr->key_table_start + entries[i].key_offset];
                        if (std::strcmp(key, "TITLE") == 0) {
                            md.title.assign(&sfo_data[hdr->data_table_start + entries[i].data_offset], entries[i].data_len);
                            while (!md.title.empty() && md.title.back() == '\0') md.title.pop_back();
                        } else if (std::strcmp(key, "DISC_ID") == 0) {
                            md.id.assign(&sfo_data[hdr->data_table_start + entries[i].data_offset], entries[i].data_len);
                            while (!md.id.empty() && md.id.back() == '\0') md.id.pop_back();
                        }
                    }
                }
            }
        }
        
        if (icon_rec.length > 0 && icon_rec.size_le < 2000000) {
            md.icon_data.resize(icon_rec.size_le);
            uint32_t icon_sectors = (icon_rec.size_le + 2047) / 2048;
            for (uint32_t i = 0; i < icon_sectors; ++i) {
                char s_buf[2048];
                if (reader.read_sector(icon_rec.lba_le + i, s_buf)) {
                    uint32_t to_copy = std::min((uint32_t)2048, (uint32_t)(icon_rec.size_le - (i * 2048)));
                    std::memcpy(md.icon_data.data() + (i * 2048), s_buf, to_copy);
                }
            }
        }
    }

    md.region = IsoProcessor::get_region(md.id);
    return md;
}

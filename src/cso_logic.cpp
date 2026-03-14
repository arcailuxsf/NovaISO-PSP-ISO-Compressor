#include "cso_logic.h"
#include <fstream>
#include <vector>
#include <zlib.h>
#include <cstdint>
#include <cstring>
#include <algorithm>

#pragma pack(push, 1)
struct CisoHeader {
    char magic[4];          // "CISO"
    uint32_t header_size;   // 24
    uint64_t total_bytes;   // Original size
    uint32_t block_size;    // 2048
    uint8_t version;        // 1
    uint8_t align;          // alignment
    uint8_t reserved[2];
};
#pragma pack(pop)

bool CsoProcessor::compress(const std::string& in_path, const std::string& out_path, int level, ProgressCallback cb) {
    std::ifstream f_in(in_path, std::ios::binary | std::ios::ate);
    if (!f_in.is_open()) return false;
    uint64_t iso_size = f_in.tellg();
    f_in.seekg(0);

    uint32_t b_size = 2048;
    uint32_t total_blocks = (uint32_t)((iso_size + b_size - 1) / b_size);
    
    // Calculate alignment (index_shift)
    // For safety and compatibility, use align=1 if file is large, otherwise 0.
    // Standard is usually 1 (2-byte alignment).
    uint8_t align = (iso_size > 0x100000000ULL) ? 1 : 1; 

    std::ofstream f_out(out_path, std::ios::binary);
    if (!f_out.is_open()) return false;

    CisoHeader hdr = {{'C','I','S','O'}, 0x18, iso_size, b_size, 1, align, {0,0}};
    f_out.write((char*)&hdr, 0x18);

    std::vector<uint32_t> index(total_blocks + 1, 0);
    // Placeholder for index
    uint64_t index_table_pos = f_out.tellp();
    f_out.write((char*)index.data(), (total_blocks + 1) * 4);

    std::vector<uint8_t> in_buf(b_size), out_buf(b_size + 128);

    for (uint32_t i = 0; i < total_blocks; ++i) {
        // Aligned padding
        while (f_out.tellp() % (1 << align) != 0) f_out.put(0);

        uint64_t current_pos = f_out.tellp();
        index[i] = (uint32_t)(current_pos >> align);

        f_in.read((char*)in_buf.data(), b_size);
        std::streamsize read = f_in.gcount();
        if (read < b_size) std::fill(in_buf.begin() + read, in_buf.end(), 0);

        z_stream strm = {0};
        // Use deflateInit2 for better control
        if (deflateInit2(&strm, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
        
        strm.next_in = (Bytef*)in_buf.data();
        strm.avail_in = b_size;
        strm.next_out = (Bytef*)out_buf.data();
        strm.avail_out = (uInt)out_buf.size();
        
        int res = deflate(&strm, Z_FINISH);
        uint32_t c_size = (uint32_t)((uInt)out_buf.size() - strm.avail_out);
        deflateEnd(&strm);

        // Core logic: only compress if it effectively saves space
        // Maxcso/isocompressor rule: compressed (including padding) must be < block_size
        if (res != Z_STREAM_END || c_size >= b_size - 12) {
            index[i] |= 0x80000000; // Bit 31: RAW
            f_out.write((char*)in_buf.data(), b_size);
        } else {
            f_out.write((char*)out_buf.data(), c_size);
        }

        if (cb && (i % 250 == 0 || i == total_blocks - 1)) cb((float)(i + 1) / total_blocks);
    }

    // Write final N+1 index entry
    while (f_out.tellp() % (1 << align) != 0) f_out.put(0);
    index[total_blocks] = (uint32_t)(f_out.tellp() >> align);

    // Update index table
    f_out.seekp(index_table_pos);
    f_out.write((char*)index.data(), (total_blocks + 1) * 4);
    f_out.close();
    return true;
}

bool CsoProcessor::decompress(const std::string& in_path, const std::string& out_path, ProgressCallback cb) {
    std::ifstream f_in(in_path, std::ios::binary);
    if (!f_in.is_open()) return false;

    CisoHeader hdr;
    f_in.read((char*)&hdr, 0x18);
    if (std::memcmp(hdr.magic, "CISO", 4) != 0) return false;

    uint32_t total_blocks = (uint32_t)((hdr.total_bytes + hdr.block_size - 1) / hdr.block_size);
    std::vector<uint32_t> index(total_blocks + 1);
    f_in.read((char*)index.data(), (total_blocks + 1) * 4);

    std::ofstream f_out(out_path, std::ios::binary);
    if (!f_out.is_open()) return false;

    std::vector<uint8_t> in_buf(hdr.block_size + 256), out_buf(hdr.block_size);

    for (uint32_t i = 0; i < total_blocks; ++i) {
        bool is_raw = index[i] & 0x80000000;
        uint64_t pos = (uint64_t)(index[i] & 0x7FFFFFFF) << hdr.align;
        uint64_t next_pos = (uint64_t)(index[i+1] & 0x7FFFFFFF) << hdr.align;
        uint32_t chunk_size = (uint32_t)(next_pos - pos);

        f_in.seekg(pos);
        
        uint32_t out_target = hdr.block_size;
        if (i == total_blocks - 1) {
            uint32_t rem = hdr.total_bytes % hdr.block_size;
            if (rem > 0) out_target = rem;
        }

        if (is_raw) {
            f_in.read((char*)out_buf.data(), out_target);
            f_out.write((char*)out_buf.data(), out_target);
        } else {
            f_in.read((char*)in_buf.data(), chunk_size);
            z_stream strm = {0};
            if (inflateInit2(&strm, -15) != Z_OK) return false;
            
            strm.next_in = (Bytef*)in_buf.data();
            strm.avail_in = chunk_size;
            strm.next_out = (Bytef*)out_buf.data();
            strm.avail_out = hdr.block_size;
            
            int res = inflate(&strm, Z_FINISH);
            inflateEnd(&strm);
            
            if (res != Z_STREAM_END && res != Z_OK) return false;
            f_out.write((char*)out_buf.data(), out_target);
        }

        if (cb && (i % 250 == 0 || i == total_blocks - 1)) cb((float)(i + 1) / total_blocks);
    }
    f_out.close();
    return true;
}

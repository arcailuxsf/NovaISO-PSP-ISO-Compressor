#ifndef ISO_LOGIC_H
#define ISO_LOGIC_H

#include <string>
#include <vector>
#include <cstdint>

struct GameMetadata {
    std::string title;
    std::string id;
    std::string region;
    std::string format;
    std::vector<uint8_t> icon_data;
    uint64_t size;
};

class IsoProcessor {
public:
    static GameMetadata get_metadata(const std::string& path);
    static std::string detect_format(const std::string& path);
    static std::string get_region(const std::string& id);
};

#endif

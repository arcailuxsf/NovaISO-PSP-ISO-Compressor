#ifndef CSO_LOGIC_H
#define CSO_LOGIC_H

#include <string>
#include <functional>
#include <vector>
#include <cstdint>

typedef std::function<void(float)> ProgressCallback;

class CsoProcessor {
public:
    static bool compress(const std::string& in_path, const std::string& out_path, int level, ProgressCallback cb);
    static bool decompress(const std::string& in_path, const std::string& out_path, ProgressCallback cb);
};

#endif

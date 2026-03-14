#ifndef CSO_METADATA_H
#define CSO_METADATA_H

#include "iso_logic.h"
#include <string>

class CsoMetadataProcessor {
public:
    static GameMetadata get_metadata(const std::string& path);
};

#endif

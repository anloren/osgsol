#ifndef OSGSOL_SCIENCE_PROCESSING_STORE_H
#define OSGSOL_SCIENCE_PROCESSING_STORE_H

#include <string>

#include "ScienceProcessingTypes.h"

namespace earthscience
{
    std::string scienceProcessingUtcNow();

    class ScienceProcessingStore
    {
    public:
        explicit ScienceProcessingStore(std::string root);

        const std::string& root() const { return _root; }
        bool prepare(std::string& error) const;
        bool save(const ScienceProcessingRecord& record,
                  std::string& error) const;
        bool load(const std::string& recordId,
                  ScienceProcessingRecord& record,
                  std::string& error) const;

    private:
        std::string _root;
    };
}

#endif

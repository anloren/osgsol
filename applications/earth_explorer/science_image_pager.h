#ifndef OSGSOL_APPLICATIONS_EARTH_EXPLORER_SCIENCE_IMAGE_PAGER_H
#define OSGSOL_APPLICATIONS_EARTH_EXPLORER_SCIENCE_IMAGE_PAGER_H

#include <osgDB/ImagePager>
#include <string>

namespace earthscience
{
    class ScienceImagePager : public osgDB::ImagePager
    {
    public:
        explicit ScienceImagePager(unsigned int totalThreads = 8)
        {
            while (_imageThreads.size() < totalThreads)
            {
                const std::string name =
                    "Science Image Thread " + std::to_string(_imageThreads.size() + 1);
                _imageThreads.push_back(
                    new ImageThread(this, ImageThread::HANDLE_ALL_REQUESTS, name));
            }
        }
    };
}

#endif

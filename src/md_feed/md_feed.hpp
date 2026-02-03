#pragma once

namespace slick::sim::md_feed {

class MDFeed
{
public:
    virtual ~MDFeed() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
};


}   // end namespace slick::sim::md_feed
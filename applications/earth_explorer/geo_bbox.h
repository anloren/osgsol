#ifndef EARTH_GEO_BBOX_H
#define EARTH_GEO_BBOX_H

#include <algorithm>
#include <cmath>
#include <vector>

namespace earthgeo
{
    struct GeoBBox
    {
        double latMin = -85.0, lonMin = -180.0;
        double latMax = 85.0, lonMax = 180.0;
    };

    inline double normalizeLongitude(double lonDeg)
    {
        double normalized = std::fmod(lonDeg + 180.0, 360.0);
        if (normalized < 0.0) normalized += 360.0;
        return normalized - 180.0;
    }

    inline double unwrapLongitudeNear(double lonDeg, double referenceDeg)
    {
        double normalized = normalizeLongitude(lonDeg);
        return normalized + 360.0 * std::round((referenceDeg - normalized) / 360.0);
    }

    inline double longitudeSpan(const GeoBBox& bbox)
    {
        double span = bbox.lonMax - bbox.lonMin;
        if (span < 0.0)
        {
            span = std::fmod(span, 360.0);
            if (span < 0.0) span += 360.0;
        }
        return span;
    }

    inline GeoBBox inflateUnwrappedBBox(const GeoBBox& bbox, double factor)
    {
        double latCenter = (bbox.latMin + bbox.latMax) * 0.5;
        double latHalfSpan = (bbox.latMax - bbox.latMin) * 0.5 * factor;
        double lonSpan = longitudeSpan(bbox);
        double lonCenter = bbox.lonMin + lonSpan * 0.5;
        double lonHalfSpan = lonSpan * 0.5 * factor;

        GeoBBox inflated;
        inflated.latMin = std::max(-85.0, latCenter - latHalfSpan);
        inflated.latMax = std::min(85.0, latCenter + latHalfSpan);
        inflated.lonMin = lonCenter - lonHalfSpan;
        inflated.lonMax = lonCenter + lonHalfSpan;
        return inflated;
    }

    inline std::vector<GeoBBox> splitAntimeridianBBox(const GeoBBox& bbox)
    {
        GeoBBox transport;
        transport.latMin = std::max(-85.0, std::min(85.0, bbox.latMin));
        transport.latMax = std::max(-85.0, std::min(85.0, bbox.latMax));
        if (transport.latMin > transport.latMax)
            std::swap(transport.latMin, transport.latMax);

        double span = longitudeSpan(bbox);
        if (span >= 360.0)
        {
            transport.lonMin = -180.0;
            transport.lonMax = 180.0;
            return std::vector<GeoBBox>(1, transport);
        }

        double start = normalizeLongitude(bbox.lonMin);
        double end = start + span;
        if (end <= 180.0)
        {
            transport.lonMin = start;
            transport.lonMax = end;
            return std::vector<GeoBBox>(1, transport);
        }

        GeoBBox first = transport, second = transport;
        first.lonMin = start;
        first.lonMax = 180.0;
        second.lonMin = -180.0;
        second.lonMax = end - 360.0;
        std::vector<GeoBBox> result;
        if (first.lonMin < first.lonMax) result.push_back(first);
        if (second.lonMin < second.lonMax) result.push_back(second);
        return result;
    }

    inline bool unwrappedBBoxNeedsRefresh(const GeoBBox& subscribed,
                                          const GeoBBox& view)
    {
        double subscribedLonSpan = longitudeSpan(subscribed);
        double subscribedLonMin = subscribed.lonMin;
        double subscribedLonMax = subscribedLonMin + subscribedLonSpan;
        double subscribedLonCenter = subscribedLonMin + subscribedLonSpan * 0.5;

        double viewLonSpan = longitudeSpan(view);
        double viewLonCenter = unwrapLongitudeNear(view.lonMin + viewLonSpan * 0.5,
                                                   subscribedLonCenter);
        double viewLatCenter = (view.latMin + view.latMax) * 0.5;
        if (viewLatCenter < subscribed.latMin || viewLatCenter > subscribed.latMax ||
            viewLonCenter < subscribedLonMin || viewLonCenter > subscribedLonMax)
            return true;

        double viewSpan = std::max(view.latMax - view.latMin, viewLonSpan);
        double subscribedSpan = std::max(subscribed.latMax - subscribed.latMin,
                                         subscribedLonSpan);
        if (viewSpan > subscribedSpan) return true;
        if (viewSpan < subscribedSpan * 0.25) return true;
        return false;
    }
}

#endif

#ifndef EARTH_AI_ORBIT_TRAJECTORY_H
#define EARTH_AI_ORBIT_TRAJECTORY_H

// Deterministic camera geometry for a genuine one-take orbit.  Unlike the
// generative-video prompt path, every output frame belongs to one mathematical
// trajectory around one immutable target.  Rendering and encoding consume this
// plan; they must never ask an image-to-video model to invent camera motion.

#include <osg/Matrixd>
#include <osg/Vec3d>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace earthai
{
    struct OneTakeOrbitSeed
    {
        osg::Vec3d eye;
        osg::Vec3d target;
        osg::Vec3d orbitAxis;
        osg::Vec3d cameraUp;
    };

    struct OneTakeOrbitFrame
    {
        double progress = 0.0;
        double azimuthRadians = 0.0;
        osg::Vec3d eye;
        osg::Vec3d target;
        osg::Vec3d cameraUp;
        osg::Matrixd viewMatrix;
    };

    struct OneTakeOrbitPlan
    {
        int durationSeconds = 0;
        int framesPerSecond = 0;
        std::vector<OneTakeOrbitFrame> frames;
    };

    inline bool oneTakeOrbitVectorFinite(const osg::Vec3d& value)
    {
        return std::isfinite(value[0]) && std::isfinite(value[1]) &&
            std::isfinite(value[2]);
    }

    inline osg::Vec3d rotateOneTakeOrbitVector(
        const osg::Vec3d& value, const osg::Vec3d& unitAxis, double angle)
    {
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        return value * cosine + (unitAxis ^ value) * sine +
            unitAxis * ((unitAxis * value) * (1.0 - cosine));
    }

    inline bool oneTakeOrbitSeedIsUsable(const OneTakeOrbitSeed& seed)
    {
        if (!oneTakeOrbitVectorFinite(seed.eye) ||
            !oneTakeOrbitVectorFinite(seed.target) ||
            !oneTakeOrbitVectorFinite(seed.orbitAxis) ||
            !oneTakeOrbitVectorFinite(seed.cameraUp))
            return false;

        osg::Vec3d axis = seed.orbitAxis;
        if (axis.normalize() <= 1.0e-9) return false;
        const osg::Vec3d offset = seed.eye - seed.target;
        if (offset.length() <= 1.0e-6 || seed.cameraUp.length() <= 1.0e-9)
            return false;

        // A camera exactly on the orbit axis has no visible azimuthal motion.
        // Reject that seed instead of claiming a stationary spin is a 360 orbit.
        const osg::Vec3d radial = offset - axis * (offset * axis);
        if (radial.length() <= 1.0e-6) return false;

        const osg::Vec3d look = seed.target - seed.eye;
        return (look ^ seed.cameraUp).length() > 1.0e-9;
    }

    inline OneTakeOrbitFrame sampleOneTakeOrbit(
        const OneTakeOrbitSeed& seed, double progress)
    {
        OneTakeOrbitFrame frame;
        frame.progress = std::max(0.0, std::min(1.0, progress));
        frame.azimuthRadians = frame.progress * 2.0 * osg::PI;

        osg::Vec3d axis = seed.orbitAxis;
        axis.normalize();
        frame.eye = seed.target + rotateOneTakeOrbitVector(
            seed.eye - seed.target, axis, frame.azimuthRadians);
        frame.target = seed.target;
        frame.cameraUp = rotateOneTakeOrbitVector(
            seed.cameraUp, axis, frame.azimuthRadians);
        frame.cameraUp.normalize();
        frame.viewMatrix = osg::Matrixd::lookAt(
            frame.eye, frame.target, frame.cameraUp);
        return frame;
    }

    inline bool makeOneTakeOrbitPlan(
        const OneTakeOrbitSeed& seed, int durationSeconds,
        int framesPerSecond, OneTakeOrbitPlan& output)
    {
        output = OneTakeOrbitPlan();
        if (!oneTakeOrbitSeedIsUsable(seed) || durationSeconds <= 0 ||
            framesPerSecond <= 0)
            return false;
        const long long count = static_cast<long long>(durationSeconds) *
            static_cast<long long>(framesPerSecond);
        if (count < 2 || count > 60LL * 120LL) return false;

        output.durationSeconds = durationSeconds;
        output.framesPerSecond = framesPerSecond;
        output.frames.reserve(static_cast<std::size_t>(count));
        for (long long index = 0; index < count; ++index)
        {
            const double progress = static_cast<double>(index) /
                static_cast<double>(count - 1);
            output.frames.push_back(sampleOneTakeOrbit(seed, progress));
        }
        return true;
    }

    inline bool oneTakeOrbitPlanIsContinuous(
        const OneTakeOrbitPlan& plan, double tolerance)
    {
        if (plan.durationSeconds <= 0 || plan.framesPerSecond <= 0 ||
            plan.frames.size() < 2 || !(tolerance >= 0.0))
            return false;
        const std::size_t expected = static_cast<std::size_t>(
            plan.durationSeconds) * static_cast<std::size_t>(
                plan.framesPerSecond);
        if (plan.frames.size() != expected) return false;

        const OneTakeOrbitFrame& first = plan.frames.front();
        const double radius = (first.eye - first.target).length();
        const double expectedStep = 2.0 * osg::PI /
            static_cast<double>(plan.frames.size() - 1);
        if (!(radius > 0.0) ||
            std::fabs(first.progress) > tolerance ||
            std::fabs(first.azimuthRadians) > tolerance)
            return false;

        for (std::size_t index = 0; index < plan.frames.size(); ++index)
        {
            const OneTakeOrbitFrame& frame = plan.frames[index];
            if (!oneTakeOrbitVectorFinite(frame.eye) ||
                !oneTakeOrbitVectorFinite(frame.target) ||
                !oneTakeOrbitVectorFinite(frame.cameraUp) ||
                (frame.target - first.target).length() > tolerance ||
                std::fabs((frame.eye - frame.target).length() - radius) >
                    tolerance)
                return false;
            if (index > 0)
            {
                const double step = frame.azimuthRadians -
                    plan.frames[index - 1].azimuthRadians;
                if (!(step > 0.0) || std::fabs(step - expectedStep) > tolerance)
                    return false;
            }
        }

        const OneTakeOrbitFrame& last = plan.frames.back();
        return std::fabs(last.progress - 1.0) <= tolerance &&
            std::fabs(last.azimuthRadians - 2.0 * osg::PI) <= tolerance &&
            (last.eye - first.eye).length() <= tolerance &&
            (last.target - first.target).length() <= tolerance &&
            (last.cameraUp - first.cameraUp).length() <= tolerance;
    }
}

#endif

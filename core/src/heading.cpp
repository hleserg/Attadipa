#include "attadipa/core/heading.h"

namespace attadipa::core {

const char* to_string(HeadingSource source)
{
    switch (source) {
        case HeadingSource::Unknown: return "Unknown";
        case HeadingSource::Magnetometer: return "Magnetometer";
        case HeadingSource::SensorFusion: return "SensorFusion";
        case HeadingSource::GnssCourseOverGround: return "GnssCourseOverGround";
        case HeadingSource::RemoteSensor: return "RemoteSensor";
    }
    return "?";
}

const char* to_string(ReferenceFrame frame)
{
    switch (frame) {
        case ReferenceFrame::WatchBody: return "WatchBody";
        case ReferenceFrame::NodeBody: return "NodeBody";
        case ReferenceFrame::CourseOverGround: return "CourseOverGround";
    }
    return "?";
}

const char* to_string(HeadingValidity validity)
{
    switch (validity) {
        case HeadingValidity::Invalid: return "Invalid";
        case HeadingValidity::NoMotion: return "NoMotion";
        case HeadingValidity::Stale: return "Stale";
        case HeadingValidity::Uncalibrated: return "Uncalibrated";
        case HeadingValidity::Valid: return "Valid";
    }
    return "?";
}

}  // namespace attadipa::core

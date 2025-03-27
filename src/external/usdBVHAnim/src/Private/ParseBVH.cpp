#include "ParseBVH.h"
#include "Parse.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>

#define CHECK_GOOD(stream) \
    if (!stream.good()) {  \
        return false;      \
    }

static const char* const c_WS = " \t\r\n";
static const char* const c_AlphaNumeric = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
static const char* const c_Double = "-0123456789.";

static void MultiplyBVHQuat(double a[4], double const b[4])
{
    double result[4];
    result[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    result[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    result[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    result[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];

    a[0] = result[0];
    a[1] = result[1];
    a[2] = result[2];
    a[3] = result[3];
}

namespace usdBVHAnimPlugin {
Parse ParseDouble(Parse cursor, double& result)
{
    constexpr size_t c_NumberBufferSize = 64;
    char numberBuffer[c_NumberBufferSize];

    const char* begin = cursor.m_Begin;
    cursor = cursor.AtLeast(1, [](Parse cursor) { return cursor.AnyOf(c_Double); });
    if (!cursor) {
        return cursor;
    }

    size_t length = cursor.m_Begin - begin;
    if (length + 1 >= c_NumberBufferSize) {
        return {};
    }

    for (size_t i = 0; i < length; ++i) {
        numberBuffer[i] = begin[i];
    }

    numberBuffer[length] = '\0';
    result = std::atof(numberBuffer);
    return cursor;
}

Parse ParseUInt(Parse cursor, unsigned int& result)
{
    if (!cursor) {
        return cursor;
    }

    constexpr int c_Base = 10;
    char* end = nullptr;
    result = std::strtoul(cursor.m_Begin, &end, c_Base);
    cursor.m_Begin = end;
    return cursor;
}

Parse ParseJointOffset(Parse cursor, BVHOffset& offset)
{
    Parse result = cursor.String("OFFSET").Skip(c_WS);
    result = ParseDouble(result, offset.m_Translation[0]).Skip(c_WS);
    result = ParseDouble(result, offset.m_Translation[1]).Skip(c_WS);
    return ParseDouble(result, offset.m_Translation[2]).Skip(c_WS);
}

Parse ParseJointChannels(Parse cursor, unsigned int& numChannels, uint32_t& channels)
{
    cursor = cursor.String("CHANNELS").Skip(c_WS);
    cursor = ParseUInt(cursor, numChannels).Skip(c_WS);
    if (!cursor) {
        return cursor;
    }

    for (size_t i = 0; i < numChannels; ++i) {
        std::string token;
        cursor = cursor.Skip(c_WS).Capture(
            token,
            [&](Parse const& cursor) {
                return cursor.AnyOf({ [&](Parse const& cursor) { return cursor.String("Xposition"); },
                    [&](Parse const& cursor) { return cursor.String("Yposition"); },
                    [&](Parse const& cursor) { return cursor.String("Zposition"); },
                    [&](Parse const& cursor) { return cursor.String("Xrotation"); },
                    [&](Parse const& cursor) { return cursor.String("Yrotation"); },
                    [&](Parse const& cursor) { return cursor.String("Zrotation"); } });
            });
        if (!cursor) {
            return cursor;
        }

        constexpr unsigned int c_BitCount = static_cast<unsigned int>(BVHChannel::BitCount);
        if (token == "Xposition") {
            channels |= static_cast<unsigned int>(BVHChannel::XPosition) << (c_BitCount * i);
        } else if (token == "Yposition") {
            channels |= static_cast<unsigned int>(BVHChannel::YPosition) << (c_BitCount * i);
        } else if (token == "Zposition") {
            channels |= static_cast<unsigned int>(BVHChannel::ZPosition) << (c_BitCount * i);
        } else if (token == "Xrotation") {
            channels |= static_cast<unsigned int>(BVHChannel::XRotation) << (c_BitCount * i);
        } else if (token == "Yrotation") {
            channels |= static_cast<unsigned int>(BVHChannel::YRotation) << (c_BitCount * i);
        } else if (token == "Zrotation") {
            channels |= static_cast<unsigned int>(BVHChannel::ZRotation) << (c_BitCount * i);
        }
    }
    return cursor.Skip(c_WS);
}

Parse ParseJointHierarchy(Parse cursor, size_t currentJointIndex, BVHDocument& document)
{
    cursor = cursor.Char('{').Skip(c_WS);
    cursor = ParseJointOffset(cursor, document.m_JointOffsets[currentJointIndex]);
    cursor = ParseJointChannels(cursor, document.m_JointNumChannels[currentJointIndex], document.m_JointChannels[currentJointIndex]);

    Parse next = cursor.String("End Site").Skip(c_WS).Char('{').Skip(c_WS);
    if (next) {
        BVHOffset offset;
        cursor = ParseJointOffset(next, offset);
        cursor = cursor.Char('}').Skip(c_WS);
    } else {
        cursor = cursor.AtLeast(0, [&](Parse const& cursor) {
            std::string childName;
            Parse result = cursor
                               .String("JOINT")
                               .Skip(c_WS)
                               .Capture(childName, [&](Parse const& cursor) {
                                   return cursor.AtLeast(1, [](Parse const& cursor) {
                                       return cursor.AnyOf(c_AlphaNumeric);
                                   });
                               })
                               .Skip(c_WS);
            ;
            if (result) {
                document.m_JointNames.push_back(childName);
                document.m_JointParents.push_back(static_cast<unsigned int>(currentJointIndex));
                document.m_JointOffsets.push_back({});
                document.m_JointNumChannels.push_back(0);
                document.m_JointChannels.push_back(0);
                result = ParseJointHierarchy(result, document.m_JointNames.size() - 1, document);
            }
            return result;
        });
    }
    return cursor.Char('}').Skip(c_WS);
}

Parse ParseMotion(Parse cursor, BVHDocument& result)
{
    cursor = cursor.String("MOTION").Skip(c_WS);

    unsigned int numFrames = 0;
    cursor = cursor.String("Frames:").Skip(c_WS);
    cursor = ParseUInt(cursor, numFrames).Skip(c_WS);

    cursor = cursor.String("Frame Time:").Skip(c_WS);
    cursor = ParseDouble(cursor, result.m_FrameTime).Skip(c_WS);

    for (size_t i = 0; i < numFrames; ++i) {
        for (size_t j = 0; j < result.m_JointChannels.size(); ++j) {
            uint32_t channels = result.m_JointChannels[j];
            BVHTransform transform = {
                { 0.0, 0.0, 0.0, 1.0 }, { result.m_JointOffsets[j].m_Translation[0], result.m_JointOffsets[j].m_Translation[1], result.m_JointOffsets[j].m_Translation[2] }
            };
            for (size_t n = 0; n < result.m_JointNumChannels[j]; ++n, channels >>= 3) {
                double value = 0.0;
                double const c_DegToRad = M_PI / 180.0;
                cursor = ParseDouble(cursor, value).Skip(c_WS);
                BVHChannel channel = channels & BVHChannel::BitMask;
                switch (channel) {
                case BVHChannel::XPosition:
                    transform.m_Translation[0] += value;
                    break;
                case BVHChannel::YPosition:
                    transform.m_Translation[1] += value;
                    break;
                case BVHChannel::ZPosition:
                    transform.m_Translation[2] += value;
                    break;
                case BVHChannel::XRotation: {
                    double quat[4] = { std::sin(value * 0.5 * c_DegToRad), 0.0f, 0.0f, std::cos(value * 0.5 * c_DegToRad) };
                    MultiplyBVHQuat(transform.m_RotationQuat, quat);
                    break;
                }
                case BVHChannel::YRotation: {
                    double quat[4] = { 0.0f, std::sin(value * 0.5 * c_DegToRad), 0.0f, std::cos(value * 0.5 * c_DegToRad) };
                    MultiplyBVHQuat(transform.m_RotationQuat, quat);
                    break;
                }
                case BVHChannel::ZRotation: {
                    double quat[4] = { 0.0f, 0.0f, std::sin(value * 0.5 * c_DegToRad), std::cos(value * 0.5 * c_DegToRad) };
                    MultiplyBVHQuat(transform.m_RotationQuat, quat);
                    break;
                }
                default:
                    break;
                };
            }
            result.m_FrameTransforms.push_back(transform);
        }
    }
    return cursor;
}

bool ParseBVH(std::istream& stream, BVHDocument& result)
{
    CHECK_GOOD(stream);

    stream.seekg(0, std::ios_base::end);
    CHECK_GOOD(stream);
    size_t const totalSize = stream.tellg();
    CHECK_GOOD(stream);
    stream.seekg(0, std::ios_base::beg);
    CHECK_GOOD(stream);

    std::vector<char> contents;
    contents.resize(totalSize);
    stream.read(contents.data(), totalSize);
    CHECK_GOOD(stream);

    result.m_JointNames.push_back({});
    result.m_JointParents.push_back(BVHDocument::c_RootParentIndex);
    result.m_JointOffsets.push_back({});
    result.m_JointNumChannels.push_back(0);
    result.m_JointChannels.push_back(0);

    Parse cursor = Parse { contents.data(), contents.data() + contents.size() }
                       .String("HIERARCHY")
                       .Skip(c_WS)
                       .String("ROOT")
                       .Skip(c_WS)
                       .Capture(result.m_JointNames[0], [=](Parse const& cursor) {
                           return cursor.AtLeast(1, [=](Parse const& cursor) {
                               return cursor.AnyOf(c_AlphaNumeric);
                           });
                       })
                       .Skip(c_WS);

    if (!cursor) {
        return false;
    }

    cursor = ParseJointHierarchy(cursor, 0, result);
    if (!cursor) {
        return false;
    }

    cursor = ParseMotion(cursor, result);
    if (!cursor) {
        return false;
    }
    return true;
}

bool ParseBVH(std::string const& filePath, BVHDocument& result)
{
    std::ifstream stream(filePath, std::ios::in | std::ios::binary);
    return ParseBVH(stream, result);
}
} // namespace usdBVHAnimPlugin
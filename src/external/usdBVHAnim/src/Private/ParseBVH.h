#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace usdBVHAnimPlugin {

//! Enumeration of supported BVH channel types.
enum class BVHChannel {
    //! Value used to signify the lack of any BVH channels.
    None = 0,
    //! A channel that represents X position
    XPosition,
    //! A channel that represents Y position
    YPosition,
    //! A channel that represents Z position
    ZPosition,
    //! A channel that represents X rotation
    XRotation,
    //! A channel that represents Y rotation
    YRotation,
    //! A channel that represents Z rotation
    ZRotation,
    //! The number of bits required to store a single channel value
    BitCount = 3,
    //! A bit-mask filled with the number of bits required to store a single channel value
    BitMask = 0b111
};

//! Convenience operator for performing bit-wise operations on `BVHChannel` values
inline BVHChannel operator&(uint32_t value, BVHChannel mask)
{
    return static_cast<BVHChannel>(static_cast<uint32_t>(value) & static_cast<uint32_t>(mask));
}

//! A structure representing a translational joint offset
struct BVHOffset {
    //! An array storing the X/Y/Z components of the joint offset
    double m_Translation[3];
};

//! A structure representing a joint transform
struct BVHTransform {
    //! An array storing the X/Y/Z/W components of the joint rotation quaternion
    double m_RotationQuat[4];
    //! An array storing the X/Y/Z components of the joint translation
    double m_Translation[3];
};

//! A structure representing an entire BVH document
struct BVHDocument {
    //! A parent index value used for root-level bones, which do not have parents.
    static constexpr int c_RootParentIndex = -1;
    //! Contains the joint names for each joint in the BVH skeleton
    std::vector<std::string> m_JointNames;
    //! Contains the joint parent indices for each joint in the BVH skeleton
    std::vector<int> m_JointParents;
    //! Contains the translational joint offsets for each joint the BVH skeleton
    std::vector<BVHOffset> m_JointOffsets;
    //! Contains the number of animated channels for each joint in the animation
    std::vector<unsigned int> m_JointNumChannels;
    //! Contains a bit-packed array of `BVHChannel` values for each joint in the
    //! animation.
    //!
    //! Each element contains 3-bits for each of the joint's channels (given by
    //! `m_JointNumChannels`), and the value of each 3-bits is one of the `BVHChannel`
    //! enumerated values.
    std::vector<uint32_t> m_JointChannels;
    //! The amount of time in seconds between each frame of the animation.
    double m_FrameTime;
    //! The animated joint transforms packed into a single vector, ordered first by
    //! frame number, then by joint, then by joint channel (given by `m_JointChannels`).
    //!
    //! e.g.
    //! * Frame 0 - Joint 0 - Channel 0
    //! * Frame 0 - Joint 0 - Channel 1
    //! * Frame 0 - Joint 1 - Channel 0
    //! * Frame 1 - Joint 0 - Channel 0
    //! * Frame 1 - Joint 0 - Channel 1
    //! * Frame 1 - Joint 1 - Channel 0
    //! * ...
    std::vector<BVHTransform> m_FrameTransforms;
};

//! Parse a BVH file at the given file path, and store the result in the given
//! `BVHDocument` structure. Returns `true` on success, or `false` on failure.
bool ParseBVH(std::string const& filePath, BVHDocument& result);

//! Parse a BVH file whose contents is in the given stream, and store the result
//! in the given `BVHDocument` structure. Returns `true` on success, or `false`
//! on failure.
bool ParseBVH(std::istream& stream, BVHDocument& result);
} // namespace usdBVHAnimPlugin

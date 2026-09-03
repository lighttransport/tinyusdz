// SPDX-License-Identifier: Apache-2.0
#include "frame_packet.hh"

#include "imgui.h"

namespace lusdview {

ImDrawData* CloneImDrawData(const ImDrawData* src) {
  if (!src || !src->Valid) return nullptr;
  ImDrawData* dst = IM_NEW(ImDrawData)();
  dst->Valid = src->Valid;
  dst->CmdListsCount = src->CmdListsCount;
  dst->TotalIdxCount = src->TotalIdxCount;
  dst->TotalVtxCount = src->TotalVtxCount;
  dst->DisplayPos = src->DisplayPos;
  dst->DisplaySize = src->DisplaySize;
  dst->FramebufferScale = src->FramebufferScale;
  dst->OwnerViewport = src->OwnerViewport;
  // ImGui 1.92 dynamic-texture system: RenderDrawData() iterates Textures to create/
  // update backend texture descriptors (notably the font atlas, used by EVERY draw via
  // its white pixel). It points at the shared, ImGui-owned GetPlatformIO().Textures[]
  // (stable across the frame), so share the pointer. Leaving it null makes the render
  // thread skip all texture setup -> the font descriptor stays empty -> every ImGui
  // draw samples an out-of-bounds descriptor -> GPU fault / VK_ERROR_DEVICE_LOST (the
  // intermittent black-capture on the threaded VK path).
  dst->Textures = src->Textures;
  // CloneOutput() deep-copies a list's Cmd/Vtx/Idx buffers (the multi-threaded
  // rendering primitive). The cloned lists are owned by `dst`.
  for (const ImDrawList* l : src->CmdLists) {
    dst->CmdLists.push_back(l->CloneOutput());
  }
  return dst;
}

void FreeImDrawData(ImDrawData* dd) {
  if (!dd) return;
  for (ImDrawList* l : dd->CmdLists) IM_DELETE(l);
  dd->CmdLists.clear();
  IM_DELETE(dd);
}

}  // namespace lusdview

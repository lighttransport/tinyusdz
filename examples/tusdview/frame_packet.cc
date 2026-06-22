// SPDX-License-Identifier: Apache-2.0
#include "frame_packet.hh"

#include "imgui.h"

namespace tusdview {

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

}  // namespace tusdview

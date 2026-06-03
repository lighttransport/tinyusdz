// SPDX-License-Identifier: Apache-2.0
// tusdview - ImGui docking GUI: dockspace, prim hierarchy browser, property
// inspector table, stats overlay, and the 3D viewport (offscreen image + Maya
// navigation).
#pragma once

#include <cstdint>
#include <string>

#include "camera_nav.hh"
#include "gpu_scene.hh"
#include "imgui.h"  // ImGuiTextFilter
#include "load_control.hh"
#include "renderer.hh"
#include "scene_loader.hh"

namespace tinyusdz {
class Prim;
namespace tydra {
struct Node;
}
}  // namespace tinyusdz

namespace tusdview {

class Gui {
 public:
  // Live status of an in-flight async load (fed by App each frame).
  struct LoadStatus {
    bool active{false};
    std::string path;
    long long meshesDone{0};
    long long meshesTotal{0};
    float elapsed{0.0f};
  };

  // (Re)bind the scene being viewed. Resets selection.
  void setScene(const LoadedScene* loaded, const DrawScene* draw);
  void setLoadStatus(const LoadStatus& s) { loadStatus_ = s; }
  void setBudget(LoadControl* b) { budget_ = b; }

  // Build all panels for one frame.
  void frame(Renderer* renderer, OrbitCamera* camera);

  // Menu/toolbar actions requested this frame (app consumes, then clearActions).
  bool wantOpen() const { return wantOpen_; }
  bool wantReload() const { return wantReload_; }
  bool wantQuit() const { return wantQuit_; }
  bool wantCancelLoad() const { return wantCancelLoad_; }
  void clearActions() {
    wantOpen_ = wantReload_ = wantQuit_ = wantCancelLoad_ = false;
  }

  // Selection: set focus by absolute prim path (meshIndex < 0 = look up by path);
  // read the current focus. Used by the GUI and the MCP server.
  void selectByPath(const std::string& absPath, int meshIndex);
  const std::string& selectedPath() const { return selPath_; }
  int selectedMeshIndex() const { return selMeshIndex_; }

 private:
  void drawDockspaceAndMenu();
  void buildDefaultLayout(unsigned int dockId);
  void drawHierarchy();
  bool drawPrimTree(const tinyusdz::Prim& prim);  // returns true if shown (filter)
  bool drawNodeTree(const tinyusdz::tydra::Node& node);
  void drawInspector();
  void drawStats();
  void drawViewport();
  void drawLoadingModal();
  void drawStageMeta();
  void handleNavigation();
  void buildHelpers();  // grid / axes / bbox / skeleton lines for the current frame
  // Pick the nearest mesh hit by a ray through viewport-local pixel (px,py).
  // Returns the DrawScene mesh index, or -1 on a miss. Per-click cost only.
  int pickMesh(float px, float py, int vpW, int vpH) const;
  // Camera framing / visibility helpers (shared by hotkeys and the View menu).
  void frameSelected();  // F: fit the selected mesh's AABB (fall back to scene)
  void frameAll();       // A: fit the whole-scene AABB
  void unhideAll();      // restore every mesh to visible

  Renderer* renderer_{nullptr};
  OrbitCamera* cam_{nullptr};
  const LoadedScene* loaded_{nullptr};
  const DrawScene* draw_{nullptr};

  // Selection
  const tinyusdz::Prim* selPrim_{nullptr};
  std::string selPath_;
  int selMeshIndex_{-1};

  RenderMode mode_{RenderMode::Shaded};
  bool showRenderNodes_{false};
  bool dockBuilt_{false};

  // Per-panel search/filter (case-insensitive substring; ImGui built-in).
  ImGuiTextFilter hierFilter_;   // Hierarchy: prim name/type/path
  ImGuiTextFilter propFilter_;   // Inspector: property name/value
  ImGuiTextFilter metaFilter_;   // Stage metadata: key/value

  // Helper display toggles (View menu).
  bool showGrid_{true};
  bool showAxes_{true};
  bool showSceneBbox_{false};
  bool showPrimBbox_{true};
  bool showSkeleton_{true};  // UsdSkel joint hierarchy as world-space lines
  std::vector<HelperVertex> helperLines_;   // depth-tested (grid/axes/bbox)
  std::vector<HelperVertex> overlayLines_;  // X-ray on top (skeleton bones)

  // Per-mesh visibility (Maya hide/show/isolate). Index i <-> draw_->meshes[i];
  // reset to all-visible on setScene. Empty == all visible.
  std::vector<uint8_t> meshVisible_;

  // Viewport interaction
  bool vpHovered_{false};
  int navMode_{0};  // 0 none, 1 orbit, 2 pan, 3 dolly

  bool wantOpen_{false};
  bool wantReload_{false};
  bool wantQuit_{false};
  bool wantCancelLoad_{false};
  float clearColor_[4]{0.12f, 0.12f, 0.13f, 1.0f};

  LoadStatus loadStatus_;
  LoadControl* budget_{nullptr};
};

}  // namespace tusdview

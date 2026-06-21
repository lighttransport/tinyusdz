// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "camera_nav.hh"
#include "gpu_scene.hh"
#include "gui_stringify.hh"
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
  struct LoadStatus {
    bool active{false};
    std::string path;
    long long meshesDone{0};
    long long meshesTotal{0};
    float elapsed{0.0f};
  };

  struct TimelineInfo {
    bool hasAnimation{false};
    double start{0.0};
    double end{0.0};
    double fps{24.0};
    double current{0.0};
    double applied{0.0};
    bool playing{false};
    bool converting{false};
  };

  struct SkinningInfo {
    SkinningMode requested{SkinningMode::Auto};
    SkinningMode effective{SkinningMode::CPU};
    std::string reason;
  };

  void setScene(const LoadedScene* loaded, const DrawScene* draw);
  void setLoadStatus(const LoadStatus& s) { loadStatus_ = s; }
  void setTimeline(const TimelineInfo& t) { timeline_ = t; }
  void setSkinning(const SkinningInfo& s) { skinning_ = s; }
  void setBudget(LoadControl* b) { budget_ = b; }

  void frame(Renderer* renderer, OrbitCamera* camera);
  void renderViewportScene();

  bool wantOpen() const { return wantOpen_; }
  bool wantReload() const { return wantReload_; }
  bool wantQuit() const { return wantQuit_; }
  bool wantCancelLoad() const { return wantCancelLoad_; }
  bool wantLoadAllPayloads() const { return wantLoadAllPayloads_; }
  std::vector<std::string> takePayloadLoadRequests() {
    return std::move(payloadLoadRequests_);
  }
  bool wantVariantSwitch() const { return wantVariantSwitch_; }
  const std::map<std::string, std::map<std::string, std::string>>&
  variantOverrides() const { return variantOverrides_; }
  bool wantTogglePlay() const { return wantTogglePlay_; }
  bool wantStop() const { return wantStop_; }
  bool wantStepForward() const { return wantStepForward_; }
  bool wantStepBackward() const { return wantStepBackward_; }
  bool hasSeek() const { return hasSeek_; }
  double seekTime() const { return seekTime_; }
  bool loopPlayback() const { return loop_; }
  float playSpeed() const { return speed_; }
  float tessellationQuality() const { return tessQuality_; }
  bool showSkeletonOverlay() const { return showSkeleton_; }
  // Manual blendshape weights from the blend-shape editor (Maya-like). Returns
  // the override map when manual mode is active, else nullptr (use animation).
  const std::unordered_map<std::string, float>* blendOverrides() const {
    return blendActive_ ? &blendWeights_ : nullptr;
  }
  // True once since the last call if a blendshape weight (or manual mode) changed
  // -- the app uses this to force a morph re-pose at the same time code.
  bool consumeBlendDirty() {
    const bool d = blendDirty_;
    blendDirty_ = false;
    return d;
  }
  // Programmatically set a manual blendshape weight (e.g. the --blend CLI option
  // for headless posing); enables manual mode and marks the morph dirty.
  void setBlendWeight(const std::string& name, float w) {
    blendWeights_[name] = w;
    blendActive_ = true;
    blendDirty_ = true;
  }
  enum class TransformMode { None, Translate, Rotate, Scale };
  TransformMode transformMode() const { return xformMode_; }
  void setTransformMode(TransformMode m) { xformMode_ = m; }
  void setRenderMode(RenderMode m) { mode_ = m; }
  RenderMode renderMode() const { return mode_; }
  bool hasSkinningModeRequest() const { return hasSkinningModeRequest_; }
  SkinningMode requestedSkinningMode() const { return requestedSkinningMode_; }
  void clearActions() {
    wantOpen_ = wantReload_ = wantQuit_ = wantCancelLoad_ = false;
    wantLoadAllPayloads_ = false;
    payloadLoadRequests_.clear();
    wantVariantSwitch_ = false;
    variantOverrides_.clear();
    wantTogglePlay_ = wantStop_ = hasSeek_ = false;
    wantStepForward_ = wantStepBackward_ = false;
    hasSkinningModeRequest_ = false;
  }

  void selectByPath(const std::string& absPath, int meshIndex);
  void clearSelection();
  void requestVariantSwitch(
      const std::string& primPath,
      const std::map<std::string, std::string>& selections) {
    variantOverrides_[primPath] = selections;
    wantVariantSwitch_ = true;
  }
  const std::string& selectedPath() const { return selPath_; }
  int selectedMeshIndex() const { return selMeshIndex_; }
  void saveCameraBookmark(int slot);
  bool loadCameraBookmark(int slot);
  bool hasCameraBookmark(int slot) const;
  bool canGoSelectionBack() const;
  bool canGoSelectionForward() const;
  bool goSelectionBack();
  bool goSelectionForward();

 private:
  void applySelection(const std::string& absPath, int meshIndex, bool recordHistory);
  void pushSelectionHistory(const std::string& absPath);
  void drawDockspaceAndMenu();
  void buildDefaultLayout(unsigned int dockId);
  void drawHierarchy();
  bool drawPrimTree(const tinyusdz::Prim& prim);
  bool drawNodeTree(const tinyusdz::tydra::Node& node);
  void drawInspector();
  // Maya-like blendshape weight editor for the selected prim (shown when the
  // selection, an ancestor, or a descendant mesh carries blendshape targets).
  void drawBlendShapeEditor();
  void drawSelectionList();
  void drawCameraPanel();
  void drawStats();
  void drawPayloads();
  void drawTimeline();
  void drawMaterialsPanel();
  void drawCompositionGraph();
  void drawViewport();
  void drawAboutModal();
  void drawLoadingModal();
  void drawStageMeta();
  void drawNavigationOverlay(const ImVec2& imageMin, const ImVec2& imageMax);
  void drawSelectionBreadcrumbs(const char* idSuffix);
  bool framePath(const std::string& absPath);
  void handleNavigation();
  void buildHelpers();
  bool meshPurposeVisible(const std::string& purpose) const;
  bool meshVisibleForView(size_t meshIndex) const;
  void buildViewVisibilityMask();
  void rebuildInspectorCache();
  void setSelectionListSingle(const std::string& absPath, int meshIndex);
  void setSelectionListFromMeshes(std::vector<int> meshIndices);
  void focusSelectionListItem(size_t index);
  void beginRegionSelection(const ImVec2& mouse);
  void updateRegionSelection(const ImVec2& mouse);
  void finishRegionSelection(const ImVec2& imageMin, int vpW, int vpH);
  std::vector<int> regionPickMeshes(const ImVec2& imageMin, int vpW, int vpH) const;
  bool meshIntersectsScreenRect(size_t meshIndex, const ImVec2& rectMin,
                                const ImVec2& rectMax, int vpW, int vpH) const;
  int pickMesh(float px, float py, int vpW, int vpH) const;
  void selectAdjacentMesh(int step);
  void applyViewPreset(CameraViewPreset preset);
  void homeView();
  void frameSelected();
  void frameAll();
  void unhideAll();

  Renderer* renderer_{nullptr};
  OrbitCamera* cam_{nullptr};
  const LoadedScene* loaded_{nullptr};
  const DrawScene* draw_{nullptr};

  struct CameraBookmark {
    bool valid{false};
    light3d::Vec3 target{0.0f, 0.0f, 0.0f};
    float yaw{0.0f};
    float pitch{0.0f};
    float distance{1.0f};
    std::string selectedPath;
  };

  const tinyusdz::Prim* selPrim_{nullptr};
  std::string selPath_;
  int selMeshIndex_{-1};

  // Blendshape editor state. blendWeights_ is keyed by BlendShape name; when
  // blendActive_ it overrides the SkelAnimation-driven weights in the morph pass.
  std::unordered_map<std::string, float> blendWeights_;
  bool blendActive_{false};
  bool blendDirty_{false};
  std::vector<std::pair<std::string, int>> selectionList_;
  std::array<CameraBookmark, 3> cameraBookmarks_{};
  std::vector<std::string> selectionHistory_;
  int selectionHistoryIndex_{-1};

  RenderMode mode_{RenderMode::Shaded};
  // Transform manipulator mode.
  TransformMode xformMode_{TransformMode::None};
  int gizmoAxis_{-1};         // -1 = none, 0=X, 1=Y, 2=Z
  bool gizmoActive_{false};   // currently dragging the gizmo
  ImVec2 gizmoMouseStart_;    // mouse position at drag start
  float gizmoStartPos_[3];    // world position at drag start
  bool showRenderNodes_{false};
  bool dockBuilt_{false};

  ImGuiTextFilter hierFilter_;
  ImGuiTextFilter propFilter_;
  ImGuiTextFilter metaFilter_;

  // View menu toggles
  bool showGrid_{true};
  bool showAxes_{true};
  bool showSceneBbox_{false};
  bool showPrimBbox_{true};
  bool showSkeleton_{true};
  bool showLights_{false};
  bool showCameras_{false};
  bool showExtent_{false};
  bool showPrimLabels_{false};
  bool showPurposeDefault_{true};
  bool showPurposeRender_{true};
  bool showPurposeProxy_{true};
  bool showPurposeGuide_{false};
  bool showNavHelp_{true};
  bool showAbout_{false};
  float tessQuality_{1.0f};
  std::vector<HelperVertex> helperLines_;
  std::vector<HelperVertex> overlayLines_;

  std::vector<uint8_t> meshVisible_;
  std::vector<uint8_t> viewVisible_;
  bool revealSelectionInHierarchy_{false};

  struct InspectorPropRow {
    std::string name;
    std::string value;
    std::string typeStr;
    std::string attrMeta;
    bool gotProperty{false};
    bool hasColor{false};
    float color[4]{0.0f, 0.0f, 0.0f, 1.0f};
  };
  const tinyusdz::Prim* inspectorCachePrim_{nullptr};
  std::string inspectorCachePath_;
  std::string inspectorCacheType_;
  std::string inspectorCacheMeta_;
  std::string inspectorCacheError_;
  std::vector<InspectorPropRow> inspectorCacheRows_;

  bool vpHovered_{false};
  int navMode_{0};
  int viewportW_{0};
  int viewportH_{0};
  bool regionSelecting_{false};
  bool regionSelectionMoved_{false};
  ImVec2 regionStart_{0.0f, 0.0f};
  ImVec2 regionEnd_{0.0f, 0.0f};

  bool wantOpen_{false};
  bool wantReload_{false};
  bool wantQuit_{false};
  bool wantCancelLoad_{false};
  bool wantLoadAllPayloads_{false};
  std::vector<std::string> payloadLoadRequests_;
  bool wantVariantSwitch_{false};
  std::map<std::string, std::map<std::string, std::string>> variantOverrides_;

  TimelineInfo timeline_;
  SkinningInfo skinning_;
  bool hasSkinningModeRequest_{false};
  SkinningMode requestedSkinningMode_{SkinningMode::Auto};
  bool wantTogglePlay_{false};
  bool wantStop_{false};
  bool wantStepForward_{false};
  bool wantStepBackward_{false};
  bool hasSeek_{false};
  double seekTime_{0.0};
  bool loop_{true};
  float speed_{1.0f};
  float clearColor_[4]{0.12f, 0.12f, 0.13f, 1.0f};

  LoadStatus loadStatus_;
  LoadControl* budget_{nullptr};
};

}  // namespace tusdview

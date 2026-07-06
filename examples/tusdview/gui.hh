// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "camera_nav.hh"
#include "frame_packet.hh"
#include "gpu_scene.hh"
#include "light3d/camera.h"  // light3d::Frustum (cull worker)
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
  ~Gui();  // joins the per-instance cull worker

  struct LoadStatus {
    bool active{false};
    std::string path;
    long long meshesDone{0};
    long long meshesTotal{0};
    float elapsed{0.0f};
  };

  // GPU-side progress shown as a non-modal viewport overlay: raster geometry/
  // texture streaming (progressive upload) and the ray-tracing scene build.
  struct UploadStatus {
    bool active{false};                 // progressive raster upload in flight
    size_t meshesDone{0}, meshesTotal{0};
    size_t texDone{0}, texTotal{0};
    size_t volDone{0}, volTotal{0};
    std::string note;                   // e.g. "Building ray-tracing scene…"
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
  void setUploadStatus(const UploadStatus& s) { upload_ = s; }
  void setTimeline(const TimelineInfo& t) { timeline_ = t; }
  void setSkinning(const SkinningInfo& s) { skinning_ = s; }
  void setBudget(LoadControl* b) { budget_ = b; }

  void frame(Renderer* renderer, OrbitCamera* camera);
  // Build the viewport render inputs. `packet` null (single-threaded) renders the
  // scene inline via renderer_->renderFrame; non-null (threaded) copies the inputs
  // into the packet and routes GPU side-effects (resize, instance visibility)
  // through postGpu_ for the render thread (renderFrame is NOT called here).
  void renderViewportScene(FramePacket* packet = nullptr);
  // Current viewport panel size in pixels (0 until the first frame lays it out).
  // Used by the interactive HIP/CUDA path to size its GPU trace to the viewport.
  void viewportPixelSize(int* w, int* h) const {
    if (w) *w = viewportW_;
    if (h) *h = viewportH_;
  }
  // Route a GPU op to the render thread (threaded); runs inline if unset.
  void setPostGpu(std::function<void(std::function<void()>)> fn) {
    postGpu_ = std::move(fn);
  }

  bool wantOpen() const { return wantOpen_; }
  // File > Open Recent: the menu populated from setRecentScenes(); when an entry
  // is clicked wantOpenRecent() is set and recentToOpen() holds its path.
  void setRecentScenes(const std::vector<std::string>& v) { recentScenes_ = v; }
  bool wantOpenRecent() const { return wantOpenRecent_; }
  const std::string& recentToOpen() const { return recentToOpen_; }
  bool wantReload() const { return wantReload_; }
  // True if the 3D viewport image was hovered last frame (used by the WebSocket
  // stream server to decide whether a browser drag navigates the camera or is an
  // ImGui widget interaction).
  bool viewportHovered() const { return vpHovered_; }
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
  // Effective global displacement scale (0 when displacement is disabled), for the
  // CUDA ray tracer which bakes displacement into geometry at build time.
  float displacementScale() const {
    return displacementEnabled_ ? displacementScale_ : 0.0f;
  }
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
  // Advance the 'w'-key wireframe cycle (0 off / 1 wire / 2 wire+shade); returns
  // the new state. Used by the MCP input tool.
  int cycleWireframe() { wireCycle_ = (wireCycle_ + 1) % 3; return wireCycle_; }
  int wireframeMode() const { return wireCycle_; }
  void setCullEnabled(bool on) { cullEnabled_ = on; }
  // Offload per-instance culling to a worker thread (interactive responsiveness).
  // Disabled in headless so screenshots stay synchronous/deterministic.
  void setCullAsync(bool on) { cullAsync_ = on; }
  bool hasSkinningModeRequest() const { return hasSkinningModeRequest_; }
  SkinningMode requestedSkinningMode() const { return requestedSkinningMode_; }
  void clearActions() {
    wantOpen_ = wantReload_ = wantQuit_ = wantCancelLoad_ = false;
    wantOpenRecent_ = false;
    wantLoadAllPayloads_ = false;
    payloadLoadRequests_.clear();
    wantVariantSwitch_ = false;
    variantOverrides_.clear();
    wantTogglePlay_ = wantStop_ = hasSeek_ = false;
    wantStepForward_ = wantStepBackward_ = false;
    hasSkinningModeRequest_ = false;
  }

  // Per-frame render stats (after the last renderViewportScene). Used by the
  // headless path to report frustum-cull effectiveness on large scenes.
  struct RenderStats {
    size_t visibleMeshes{0};
    size_t totalMeshes{0};
    size_t visibleInstances{0};
    size_t totalInstances{0};
    size_t drawnTriangles{0};
    size_t drawCalls{0};
  };
  RenderStats renderStats() const {
    return {statVisibleMeshes_, draw_ ? draw_->meshes.size() : 0,
            statVisibleInstances_, statTotalInstances_,
            statNonInstTris_ + statInstTris_, statDrawCalls_};
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
  void drawProgressOverlay();  // non-modal GPU upload / RT-build progress
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
  // When the selection is a GeomSubset, the triangle vertex indices of its faces
  // (built via the parent mesh's sourceFaceId) so the highlight outlines just the
  // subset; highlightSubsetMesh_ is that parent mesh's draw index.
  std::vector<uint32_t> highlightSubsetIndices_;
  int highlightSubsetMesh_{-1};
  // World-space orange edge lines of the highlighted triangles (whole mesh or
  // subset) for the Vulkan backend's line-pipeline highlight.
  std::vector<HelperVertex> highlightLinesData_;
  void rebuildSubsetHighlight();

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
  // Wireframe overlay state cycled by the 'w' key: 0 off / 1 wire-only / 2 wire+shaded.
  int wireCycle_{0};
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
  bool showNavHelp_{false};
  bool showAbout_{false};
  bool cullEnabled_{true};  // per-mesh + per-instance frustum culling (View menu)
  // Per-frame render stats (computed in buildViewVisibilityMask + the per-instance
  // cull pass; rendered by drawStats). "visible" reflects frustum culling.
  size_t statVisibleMeshes_{0};
  size_t statTotalInstances_{0};
  size_t statVisibleInstances_{0};  // owned by cullInstances
  size_t statNonInstTris_{0};       // visible non-instanced triangles (per-mesh pass)
  size_t statInstTris_{0};          // visible instanced triangles (cullInstances)
  size_t statDrawCalls_{0};
  // Per-instance frustum culling (A4): compact each instanced prototype's visible
  // instances into the renderer's instance buffer. Re-run only when the view or
  // cull state changes (cullInstances), keyed by lastCull*. The CPU frustum
  // test + compaction runs on a WORKER thread (Stage-1 offload, large instanced
  // scenes); cullInstances() polls the worker, applies its compacted result on the
  // main thread (updateInstanceVisibility is a GPU call), and relaunches on change.
  // Run a GPU op on the render thread when threaded, else inline.
  std::function<void(std::function<void()>)> postGpu_;
  void gpu(std::function<void()> op) {
    if (postGpu_) postGpu_(std::move(op)); else op();
  }
  void cullInstances();
  void cullInstancesSync();  // synchronous compaction + apply (headless / cullAsync_ off)
  void joinCullWorker();     // join + reset; called from setScene + ~Gui
  void cullWorkerMain();     // runs on the worker thread (CPU only, reads snapshots)
  bool cullAsync_{true};
  float lastCullVP_[16]{};
  bool lastCullValid_{false};
  bool lastCullEnabled_{false};
  bool lastCullRasterLod_{false};
  const DrawScene* lastCullDraw_{nullptr};
  // One mesh's compacted visible instances, produced by the worker, applied on main.
  struct CullJobMesh {
    size_t meshIndex{0};
    std::vector<float> xforms;   // 12 floats/visible-instance
    std::vector<float> colors;   // 3 floats/visible-instance (empty when none)
    std::vector<float> opacities;  // 1 float/visible-instance (empty when none)
    uint32_t count{0};
    bool hasColors{false};
    bool hasOpacities{false};
  };
  // Frustum-test + compact one instanced mesh's visible instances into `out`
  // (shared by the sync + worker paths). cullEnabled=false -> the full set. When
  // `grid` is non-null (a coarse per-prototype instance grid), whole off-screen
  // cells are rejected and fully-inside cells accepted without per-instance tests
  // -- so cull cost scales with the visible cell set, not the total instance count.
  // When lodCam.lodEnabled, instances are also size-classified: sub-pixel ones are
  // dropped and (with proxyOut + lodCam.proxyEnabled) small ones become box proxies
  // appended to proxyOut (accumulated across prototypes by the caller).
  static void compactMeshInstances(const DrawMeshCPU& m, const light3d::Frustum& fr,
                                   bool cullEnabled, const RtLodGrid* grid,
                                   const RtLodCamera& lodCam, CullJobMesh* out,
                                   CullJobMesh* proxyOut);
  // Raster view-dependent LOD (optimization B): drop sub-pixel instances + collapse
  // small ones to shared box proxies, cutting both the uploaded instance count and
  // the rasterised geometry. Off by default (exact parity). proxyEnabled is gated on
  // the renderer supporting the box-proxy draw (GL); else cull-only.
  bool rasterLodEnabled_{false};
  float rasterLodFullPx_{48.0f};
  float rasterLodCullPx_{1.5f};
  CullJobMesh proxyResult_;        // accumulated box proxies (sync path / applied)
  RtLodCamera cullJobLodCam_;      // snapshot for the worker
  CullJobMesh cullJobProxy_;       // worker-accumulated box proxies
 public:
  void setRasterLod(bool on, float fullPx, float cullPx) {
    rasterLodEnabled_ = on;
    if (fullPx > 0.f) rasterLodFullPx_ = fullPx;
    if (cullPx >= 0.f) rasterLodCullPx_ = cullPx;
  }
 private:
  // Build the LOD camera (thresholds + projection focal length) for this cull.
  RtLodCamera buildRasterLodCam() const;
  // Coarse instance grids (one per mesh; empty/invalid for non-instanced or small
  // prototypes), built once per scene for compactMeshInstances cell rejection.
  // Read-only after build, so the cull worker shares them via cullJobGrids_.
  std::vector<RtLodGrid> instGrids_;
  const DrawScene* instGridsFor_{nullptr};
  void ensureInstanceGrids();  // (re)build instGrids_ when draw_ changes
  const std::vector<RtLodGrid>* cullJobGrids_{nullptr};
  std::thread cullThread_;
  std::atomic<bool> cullRunning_{false};
  std::atomic<bool> cullDone_{false};
  std::vector<CullJobMesh> cullJobResult_;     // worker writes, main reads after done
  size_t cullJobVisInstances_{0};
  size_t cullJobInstTris_{0};
  // Worker inputs snapshotted by main before launch (so the worker races nothing).
  light3d::Mat4 cullJobVP_{};
  std::vector<uint8_t> cullJobViewVisible_;
  bool cullJobEnabled_{false};
  const DrawScene* cullJobDraw_{nullptr};
  float tessQuality_{1.0f};
  // UsdPreviewSurface displacement (raster preview). enabled = master toggle;
  // scale = global multiplier; maxTessLevel > 1 enables GPU tessellation for
  // adaptive sub-triangle detail (1 = coarse per-vertex displacement only).
  bool displacementEnabled_{true};
  float displacementScale_{1.0f};
  int maxTessLevel_{1};
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
  bool wantOpenRecent_{false};
  std::string recentToOpen_;
  std::vector<std::string> recentScenes_;  // newest first (from App config)
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
  UploadStatus upload_;
  LoadControl* budget_{nullptr};
};

}  // namespace tusdview

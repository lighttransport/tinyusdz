// SPDX-License-Identifier: Apache-2.0
#include "gui.hh"

#include <algorithm>
#include <cmath>

#include "core/prim.hh"
#include "gui_stringify.hh"
#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder*
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"

namespace tusdview {

namespace tydra = tinyusdz::tydra;

namespace {

const char* NodeTypeName(tydra::NodeType t) {
  switch (t) {
    case tydra::NodeType::Xform: return "Xform";
    case tydra::NodeType::Mesh: return "Mesh";
    case tydra::NodeType::Camera: return "Camera";
    case tydra::NodeType::SkelRoot: return "SkelRoot";
    case tydra::NodeType::Skeleton: return "Skeleton";
    case tydra::NodeType::PointLight: return "PointLight";
    case tydra::NodeType::DirectionalLight: return "DirectionalLight";
    case tydra::NodeType::EnvmapLight: return "EnvmapLight";
    case tydra::NodeType::RectLight: return "RectLight";
    case tydra::NodeType::DiskLight: return "DiskLight";
    case tydra::NodeType::CylinderLight: return "CylinderLight";
    case tydra::NodeType::GeometryLight: return "GeometryLight";
    default: return "Node";
  }
}

// Greyed, word-wrapped hint text (TextDisabled does not wrap and would clip in
// a narrow panel).
void HintWrapped(const char* s) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
  ImGui::TextWrapped("%s", s);
  ImGui::PopStyleColor();
}

const tinyusdz::Prim* FindPrimByPath(const tinyusdz::Prim& prim,
                                     const std::string& path) {
  if (prim.absolute_path().full_path_name() == path) return &prim;
  for (const auto& c : prim.children()) {
    if (const tinyusdz::Prim* p = FindPrimByPath(c, path)) return p;
  }
  return nullptr;
}

// --- Hierarchy search predicates (name + type + abs path) ---
bool PrimPasses(ImGuiTextFilter& f, const tinyusdz::Prim& prim) {
  std::string typeName = prim.prim_type_name();
  if (typeName.empty()) typeName = prim.type_name();
  std::string s = prim.element_name() + " " + typeName + " " +
                  prim.absolute_path().full_path_name();
  return f.PassFilter(s.c_str());
}
bool PrimSubtreePasses(ImGuiTextFilter& f, const tinyusdz::Prim& prim) {
  if (PrimPasses(f, prim)) return true;
  for (const auto& c : prim.children()) {
    if (PrimSubtreePasses(f, c)) return true;
  }
  return false;
}
bool NodePasses(ImGuiTextFilter& f, const tydra::Node& node) {
  std::string s = node.prim_name + " " + node.abs_path + " " +
                  NodeTypeName(node.nodeType);
  return f.PassFilter(s.c_str());
}
bool NodeSubtreePasses(ImGuiTextFilter& f, const tydra::Node& node) {
  if (NodePasses(f, node)) return true;
  for (const auto& c : node.children) {
    if (NodeSubtreePasses(f, c)) return true;
  }
  return false;
}

}  // namespace

void Gui::setScene(const LoadedScene* loaded, const DrawScene* draw) {
  loaded_ = loaded;
  draw_ = draw;
  selPrim_ = nullptr;
  selPath_.clear();
  selMeshIndex_ = -1;
  // Reset per-mesh visibility to all-visible for the new scene.
  meshVisible_.assign(draw_ ? draw_->meshes.size() : 0, uint8_t{1});
  // Start with nothing selected; the user selects via the viewport or hierarchy.
}

void Gui::frame(Renderer* renderer, OrbitCamera* camera) {
  renderer_ = renderer;
  cam_ = camera;
  drawDockspaceAndMenu();
  drawHierarchy();
  drawInspector();
  drawStageMeta();
  drawStats();
  drawViewport();
  drawLoadingModal();
}

void Gui::drawLoadingModal() {
  const char* kId = "Loading USD##tusdview";
  if (loadStatus_.active && !ImGui::IsPopupOpen(kId)) {
    ImGui::OpenPopup(kId);
  }
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(kId, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoMove)) {
    ImGui::TextUnformatted("Loading on a background thread...");
    ImGui::TextDisabled("%s", loadStatus_.path.c_str());
    ImGui::Separator();
    if (loadStatus_.meshesTotal > 0) {
      const float frac = static_cast<float>(loadStatus_.meshesDone) /
                         static_cast<float>(loadStatus_.meshesTotal);
      ImGui::ProgressBar(frac, ImVec2(300, 0));
      ImGui::Text("Converting meshes: %lld / %lld", loadStatus_.meshesDone,
                  loadStatus_.meshesTotal);
    } else {
      ImGui::Text("Parsing / converting...");
    }
    ImGui::Text("Elapsed: %.1f s", loadStatus_.elapsed);
    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      wantCancelLoad_ = true;
    }
    if (!loadStatus_.active) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void Gui::buildDefaultLayout(unsigned int dockId) {
  ImGui::DockBuilderRemoveNode(dockId);
  ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);

  ImGuiID center = dockId;
  ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
  ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);
  ImGuiID rightBottom =
      ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.40f, nullptr, &right);

  ImGui::DockBuilderDockWindow("Hierarchy", left);
  ImGui::DockBuilderDockWindow("Stats", left);
  ImGui::DockBuilderDockWindow("Inspector", right);
  ImGui::DockBuilderDockWindow("Stage", rightBottom);
  ImGui::DockBuilderDockWindow("Viewport", center);
  ImGui::DockBuilderFinish(dockId);
}

void Gui::drawDockspaceAndMenu() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowViewport(vp->ID);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoBringToFrontOnFocus |
                           ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
                           ImGuiWindowFlags_MenuBar;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##TusdviewDockHost", nullptr, flags);
  ImGui::PopStyleVar(3);

  const ImGuiID dockId = ImGui::GetID("TusdviewDockspace");
  ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
  if (!dockBuilt_) {
    buildDefaultLayout(dockId);
    dockBuilt_ = true;
  }

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open...", "Ctrl+O")) wantOpen_ = true;
      if (ImGui::MenuItem("Reload", "Ctrl+R", false, loaded_ != nullptr)) wantReload_ = true;
      ImGui::Separator();
      if (ImGui::MenuItem("Quit", "Esc")) wantQuit_ = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      bool shaded = mode_ == RenderMode::Shaded;
      bool wire = mode_ == RenderMode::Wireframe;
      if (ImGui::MenuItem("Shaded", nullptr, shaded)) mode_ = RenderMode::Shaded;
      if (ImGui::MenuItem("Wireframe", nullptr, wire)) mode_ = RenderMode::Wireframe;
      ImGui::Separator();
      // Ray tracing (Vulkan only; disabled when the device/build can't do it).
      // The checkmark mirrors the renderer's actual technique.
      const bool rtAvail = renderer_ && renderer_->rayTracingAvailable();
      const bool rtOn = renderer_ && renderer_->rayTracingActive();
      if (ImGui::MenuItem("Ray tracing (Vulkan)", nullptr, rtOn, rtAvail)) {
        if (renderer_) renderer_->setRayTracing(!rtOn);
      }
      ImGui::Separator();
      const bool haveBounds = draw_ && draw_->hasBounds;
      if (ImGui::MenuItem("Frame selected", "F", false, haveBounds)) frameSelected();
      if (ImGui::MenuItem("Frame all", "A", false, haveBounds)) frameAll();
      ImGui::Separator();
      // Hide family (Maya): also available via H / Ctrl+H / Shift+H / Alt+H.
      const bool haveSel = selMeshIndex_ >= 0 &&
                           static_cast<size_t>(selMeshIndex_) < meshVisible_.size();
      if (ImGui::MenuItem("Hide selection", "Ctrl+H", false, haveSel)) {
        meshVisible_[static_cast<size_t>(selMeshIndex_)] = 0;
      }
      if (ImGui::MenuItem("Isolate selection", "Alt+H", false, haveSel)) {
        for (size_t i = 0; i < meshVisible_.size(); ++i)
          meshVisible_[i] = (static_cast<int>(i) == selMeshIndex_) ? 1 : 0;
      }
      if (ImGui::MenuItem("Unhide all", nullptr, false, !meshVisible_.empty())) {
        unhideAll();
      }
      ImGui::Checkbox("Show RenderScene nodes", &showRenderNodes_);
      ImGui::Separator();
      ImGui::MenuItem("Grid", nullptr, &showGrid_);
      ImGui::MenuItem("Axes", nullptr, &showAxes_);
      ImGui::MenuItem("Scene bounds", nullptr, &showSceneBbox_);
      ImGui::MenuItem("Selected bounds", nullptr, &showPrimBbox_);
      ImGui::MenuItem("Skeleton", nullptr, &showSkeleton_);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
  ImGui::End();
}

void Gui::selectByPath(const std::string& absPath, int meshIndex) {
  selPath_ = absPath;
  selMeshIndex_ = meshIndex;
  selPrim_ = nullptr;
  if (loaded_) {
    for (const auto& root : loaded_->stage.root_prims()) {
      if (const tinyusdz::Prim* p = FindPrimByPath(root, absPath)) {
        selPrim_ = p;
        break;
      }
    }
  }
  if (meshIndex < 0 && draw_) {
    for (size_t i = 0; i < draw_->meshes.size(); ++i) {
      if (draw_->meshes[i].absPath == absPath) {
        selMeshIndex_ = static_cast<int>(i);
        break;
      }
    }
  }
}

void Gui::clearSelection() {
  selPath_.clear();
  selMeshIndex_ = -1;
  selPrim_ = nullptr;
}

bool Gui::drawPrimTree(const tinyusdz::Prim& prim) {
  // When filtering, hide whole subtrees that contain no match; expand the rest
  // so matches are visible in context.
  const bool filtering = hierFilter_.IsActive();
  if (filtering && !PrimSubtreePasses(hierFilter_, prim)) return false;

  ImGui::PushID(static_cast<const void*>(&prim));
  std::string typeName = prim.prim_type_name();
  if (typeName.empty()) typeName = prim.type_name();
  std::string label = prim.element_name();
  if (label.empty()) label = "<root>";
  if (!typeName.empty()) label += "  [" + typeName + "]";

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick |
                             ImGuiTreeNodeFlags_SpanAvailWidth;
  const bool leaf = prim.children().empty();
  if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
  if (selPrim_ == &prim) flags |= ImGuiTreeNodeFlags_Selected;

  if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    selectByPath(prim.absolute_path().full_path_name(), -1);
    selPrim_ = &prim;
  }
  if (open) {
    for (const auto& c : prim.children()) drawPrimTree(c);
    ImGui::TreePop();
  }
  ImGui::PopID();
  return true;
}

bool Gui::drawNodeTree(const tydra::Node& node) {
  const bool filtering = hierFilter_.IsActive();
  if (filtering && !NodeSubtreePasses(hierFilter_, node)) return false;

  ImGui::PushID(static_cast<const void*>(&node));
  std::string label = node.prim_name.empty() ? "<node>" : node.prim_name;
  label += "  {";
  label += NodeTypeName(node.nodeType);
  label += "}";

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick |
                             ImGuiTreeNodeFlags_SpanAvailWidth;
  const bool leaf = node.children.empty();
  if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
  if (!selPath_.empty() && selPath_ == node.abs_path) flags |= ImGuiTreeNodeFlags_Selected;

  if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    const int meshIdx = (node.nodeType == tydra::NodeType::Mesh) ? node.id : -1;
    selectByPath(node.abs_path, meshIdx);
  }
  if (open) {
    for (const auto& c : node.children) drawNodeTree(c);
    ImGui::TreePop();
  }
  ImGui::PopID();
  return true;
}

void Gui::drawHierarchy() {
  ImGui::Begin("Hierarchy");
  if (loaded_ && loaded_->ok) {
    hierFilter_.Draw("Search", -1.0f);  // name / type / path
    ImGui::Separator();
    if (showRenderNodes_) {
      for (const auto& n : loaded_->render.nodes) drawNodeTree(n);
    } else {
      for (const auto& root : loaded_->stage.root_prims()) drawPrimTree(root);
    }
  } else {
    HintWrapped("No scene loaded.");
    HintWrapped("File > Open... to load a USD file.");
  }
  ImGui::End();
}

void Gui::drawInspector() {
  ImGui::Begin("Inspector");
  if (selPrim_) {
    ImGui::TextWrapped("%s", selPath_.c_str());
    std::string typeName = selPrim_->prim_type_name();
    if (typeName.empty()) typeName = selPrim_->type_name();
    ImGui::TextDisabled("Type: %s", typeName.c_str());

    // Prim metadata (kind/active/hidden/displayName/doc/...).
    const std::string primMeta = PrimMetaSummary(*selPrim_);
    if (!primMeta.empty() &&
        ImGui::CollapsingHeader("Prim metadata", ImGuiTreeNodeFlags_DefaultOpen)) {
      HintWrapped(primMeta.c_str());
    }
    ImGui::Separator();
    propFilter_.Draw("Search##props", -1.0f);  // property name / value

    std::vector<std::string> names;
    std::string err;
    if (tydra::GetPropertyNames(*selPrim_, &names, &err)) {
      if (ImGui::BeginTable("##props", 2,
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 8.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const std::string& name : names) {
          tinyusdz::Property prop;
          std::string perr;
          const bool gotProp = tydra::GetProperty(*selPrim_, name, &prop, &perr);
          const std::string valStr =
              gotProp ? PropertyToString(prop) : std::string("<error>");
          // Filter rows by property name or value.
          if (propFilter_.IsActive() && !propFilter_.PassFilter(name.c_str()) &&
              !propFilter_.PassFilter(valStr.c_str())) {
            continue;
          }
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(name.c_str());
          ImGui::TableSetColumnIndex(1);
          if (gotProp) {
            ImGui::TextWrapped("%s", valStr.c_str());
            if (prop.is_attribute()) {
              const std::string am = AttrMetaSummary(prop.get_attribute());
              if (!am.empty()) HintWrapped(am.c_str());
            }
          } else {
            ImGui::TextDisabled("<error>");
          }
        }
        ImGui::EndTable();
      }
    } else {
      HintWrapped("No properties.");
    }
  } else if (!selPath_.empty()) {
    ImGui::TextWrapped("%s", selPath_.c_str());
    HintWrapped("(RenderScene node; no matching Stage prim)");
  } else {
    HintWrapped("Select a prim in the Hierarchy.");
  }
  ImGui::End();
}

void Gui::drawStats() {
  ImGui::Begin("Stats");
  ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate,
              1000.0f / ImGui::GetIO().Framerate);
  ImGui::Text("Backend: %s", renderer_ ? renderer_->caps().backend_name : "?");
  ImGui::Separator();
  if (draw_) {
    ImGui::Text("Meshes: %zu", draw_->meshes.size());
    ImGui::Text("Triangles: %zu", draw_->triangleCount);
    ImGui::Text("Materials: %zu", draw_->materials.size());
    ImGui::Text("Textures: %zu", draw_->textures.size());
    if (draw_->hasBounds) {
      ImGui::Text("Bounds: [%.2f %.2f %.2f] - [%.2f %.2f %.2f]", draw_->aabbMin[0],
                  draw_->aabbMin[1], draw_->aabbMin[2], draw_->aabbMax[0],
                  draw_->aabbMax[1], draw_->aabbMax[2]);
    }
    if (draw_->truncated) {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.25f, 1.0f),
                         "Scene TRUNCATED to fit the render budget.");
      ImGui::TextDisabled("(raise the budget below, then Reload)");
    }
    if (!draw_->skipped.empty()) {
      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Skipped (%zu):",
                         draw_->skipped.size());
      for (const std::string& s : draw_->skipped) ImGui::BulletText("%s", s.c_str());
    }
  }
  if (loaded_ && !loaded_->warn.empty()) {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Warnings:");
    ImGui::TextWrapped("%s", loaded_->warn.c_str());
  }
  if (loaded_ && !loaded_->ok && !loaded_->err.empty()) {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Load error:");
    ImGui::TextWrapped("%s", loaded_->err.c_str());
  }

  if (budget_) {
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Render budget")) {
      int triM = static_cast<int>(budget_->maxTriangles / 1000000ull);
      if (ImGui::InputInt("Max triangles (M)", &triM)) {
        budget_->maxTriangles = static_cast<size_t>(triM < 1 ? 1 : triM) * 1000000ull;
      }
      int vbMB = static_cast<int>(budget_->maxVertexBytes / (1024ull * 1024ull));
      if (ImGui::InputInt("Max vertex mem (MB)", &vbMB)) {
        budget_->maxVertexBytes =
            static_cast<size_t>(vbMB < 16 ? 16 : vbMB) * 1024ull * 1024ull;
      }
      float t = static_cast<float>(budget_->convertTimeBudgetSec);
      if (ImGui::InputFloat("Convert time budget (s, 0=off)", &t)) {
        budget_->convertTimeBudgetSec = t < 0.0f ? 0.0f : t;
      }
      ImGui::TextDisabled("Applies to the next load (File > Reload).");
    }
  }
  ImGui::End();
}

void Gui::handleNavigation() {
  if (!cam_) return;
  ImGuiIO& io = ImGui::GetIO();
  const bool alt = io.KeyAlt;

  if (navMode_ == 0 && vpHovered_ && alt) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) navMode_ = 1;
    else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) navMode_ = 2;
    else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) navMode_ = 3;
  }
  if (navMode_ != 0) {
    const ImVec2 d = io.MouseDelta;
    if (navMode_ == 1) cam_->orbit(d.x, d.y);
    else if (navMode_ == 2) cam_->pan(d.x, d.y);
    else if (navMode_ == 3) cam_->dolly((d.x - d.y) * 0.05f);
    const bool anyDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                         ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                         ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (!anyDown) navMode_ = 0;
  }

  if (vpHovered_ && io.MouseWheel != 0.0f) cam_->dolly(io.MouseWheel);

  // Maya-style hotkeys (viewport hovered, no text field focused).
  if (vpHovered_ && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_F)) frameSelected();  // frame selection
    if (ImGui::IsKeyPressed(ImGuiKey_A)) frameAll();        // frame all

    // Hide family. Needs a valid selected mesh (except none is required for the
    // plain toggle/show, but those are no-ops without a selection too).
    if (ImGui::IsKeyPressed(ImGuiKey_H)) {
      const int sel = selMeshIndex_;
      const bool haveSel =
          sel >= 0 && static_cast<size_t>(sel) < meshVisible_.size();
      if (io.KeyAlt) {
        // Alt+H: isolate the selection (hide everything else).
        if (haveSel) {
          for (size_t i = 0; i < meshVisible_.size(); ++i)
            meshVisible_[i] = (static_cast<int>(i) == sel) ? 1 : 0;
        }
      } else if (io.KeyCtrl) {
        if (haveSel) meshVisible_[static_cast<size_t>(sel)] = 0;  // hide selection
      } else if (io.KeyShift) {
        if (haveSel) meshVisible_[static_cast<size_t>(sel)] = 1;  // show selection
      } else if (haveSel) {
        // Plain H: toggle the selection's visibility.
        meshVisible_[static_cast<size_t>(sel)] =
            meshVisible_[static_cast<size_t>(sel)] ? 0 : 1;
      }
    }
  }
}

void Gui::frameSelected() {
  if (!cam_ || !draw_) return;
  const int sel = selMeshIndex_;
  if (sel >= 0 && static_cast<size_t>(sel) < draw_->meshes.size()) {
    const DrawMeshCPU& m = draw_->meshes[static_cast<size_t>(sel)];
    cam_->fitToScene(m.aabbMin, m.aabbMax);
  } else if (draw_->hasBounds) {
    cam_->fitToScene(draw_->aabbMin, draw_->aabbMax);
  }
}

void Gui::frameAll() {
  if (cam_ && draw_ && draw_->hasBounds) {
    cam_->fitToScene(draw_->aabbMin, draw_->aabbMax);
  }
}

void Gui::unhideAll() {
  for (auto& v : meshVisible_) v = 1;
}

int Gui::pickMesh(float px, float py, int vpW, int vpH) const {
  if (!cam_ || !renderer_ || !draw_ || draw_->meshes.empty() || vpW <= 0 ||
      vpH <= 0) {
    return -1;
  }

  // Build a world-space ray through the clicked pixel. The displayed image is
  // upright on both backends (GL flips via UVs, VK via a negative-height
  // viewport), so screen-top maps to NDC y = +1 regardless of flipViewportV.
  const bool z01 = renderer_->caps().usesZeroToOneDepth;
  const light3d::Mat4 V = cam_->view();
  const light3d::Mat4 P = cam_->proj(z01);
  const light3d::Mat4 invVP = (P * V).inverse();

  const float ndcx = 2.0f * (px / static_cast<float>(vpW)) - 1.0f;
  const float ndcy = 1.0f - 2.0f * (py / static_cast<float>(vpH));

  auto unproject = [&](float nz) -> light3d::Vec3 {
    const float* m = invVP.m;  // column-major: m[col*4 + row]
    const float ox = m[0] * ndcx + m[4] * ndcy + m[8] * nz + m[12];
    const float oy = m[1] * ndcx + m[5] * ndcy + m[9] * nz + m[13];
    const float oz = m[2] * ndcx + m[6] * ndcy + m[10] * nz + m[14];
    const float ow = m[3] * ndcx + m[7] * ndcy + m[11] * nz + m[15];
    const float inv = (ow != 0.0f) ? 1.0f / ow : 1.0f;
    return {ox * inv, oy * inv, oz * inv};
  };

  const light3d::Vec3 nearW = unproject(z01 ? 0.0f : -1.0f);
  const light3d::Vec3 farW = unproject(1.0f);
  const light3d::Vec3 ro = cam_->eye();
  const light3d::Vec3 rd = light3d::normalize(farW - nearW);

  // Slab test against a world-space AABB; returns true if the ray hits within
  // [0, tMax) and never starts the search past an already-closer hit.
  const float invdx = (rd.x != 0.0f) ? 1.0f / rd.x : 1e30f;
  const float invdy = (rd.y != 0.0f) ? 1.0f / rd.y : 1e30f;
  const float invdz = (rd.z != 0.0f) ? 1.0f / rd.z : 1e30f;
  auto hitAabb = [&](const float mn[3], const float mx[3], float tMax) -> bool {
    float t1 = (mn[0] - ro.x) * invdx, t2 = (mx[0] - ro.x) * invdx;
    float tmin = std::min(t1, t2), tmax = std::max(t1, t2);
    t1 = (mn[1] - ro.y) * invdy; t2 = (mx[1] - ro.y) * invdy;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));
    t1 = (mn[2] - ro.z) * invdz; t2 = (mx[2] - ro.z) * invdz;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));
    return tmax >= std::max(tmin, 0.0f) && tmin < tMax;
  };

  // Moller-Trumbore ray/triangle in world space.
  auto rayTri = [&](const light3d::Vec3& a, const light3d::Vec3& b,
                    const light3d::Vec3& c, float& tOut) -> bool {
    const light3d::Vec3 e1 = b - a, e2 = c - a;
    const light3d::Vec3 pv = light3d::cross(rd, e2);
    const float det = light3d::dot(e1, pv);
    if (std::fabs(det) < 1e-9f) return false;  // parallel
    const float invDet = 1.0f / det;
    const light3d::Vec3 tv = ro - a;
    const float u = light3d::dot(tv, pv) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    const light3d::Vec3 qv = light3d::cross(tv, e1);
    const float vv = light3d::dot(rd, qv) * invDet;
    if (vv < 0.0f || u + vv > 1.0f) return false;
    const float t = light3d::dot(e2, qv) * invDet;
    if (t <= 1e-5f) return false;  // behind the ray origin
    tOut = t;
    return true;
  };

  int best = -1;
  float bestT = 1e30f;
  for (size_t mi = 0; mi < draw_->meshes.size(); ++mi) {
    // Skip hidden meshes (they aren't drawn, so they shouldn't be pickable).
    if (mi < meshVisible_.size() && !meshVisible_[mi]) continue;
    const DrawMeshCPU& m = draw_->meshes[mi];
    if (m.vertices.empty() || m.indices.size() < 3) continue;
    if (!hitAabb(m.aabbMin, m.aabbMax, bestT)) continue;

    light3d::Mat4 W;
    for (int k = 0; k < 16; ++k) W.m[k] = m.world[k];
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
      const DrawVertex& va = m.vertices[m.indices[i + 0]];
      const DrawVertex& vb = m.vertices[m.indices[i + 1]];
      const DrawVertex& vc = m.vertices[m.indices[i + 2]];
      const light3d::Vec3 a = light3d::transformPoint(W, {va.px, va.py, va.pz});
      const light3d::Vec3 b = light3d::transformPoint(W, {vb.px, vb.py, vb.pz});
      const light3d::Vec3 c = light3d::transformPoint(W, {vc.px, vc.py, vc.pz});
      float t;
      if (rayTri(a, b, c, t) && t < bestT) {
        bestT = t;
        best = static_cast<int>(mi);
      }
    }
  }
  return best;
}

void Gui::drawViewport() {
  // Navigate using last frame's hover state, then render with the updated camera.
  handleNavigation();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("Viewport");
  ImGui::PopStyleVar();

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const int w = static_cast<int>(avail.x);
  const int h = static_cast<int>(avail.y);

  if (renderer_ && cam_ && w > 0 && h > 0) {
    cam_->setAspect(static_cast<float>(w) / static_cast<float>(h));
    renderer_->resizeViewport(w, h);

    const light3d::Mat4 viewM = cam_->view();
    const light3d::Mat4 projM = cam_->proj(renderer_->caps().usesZeroToOneDepth);
    const light3d::Vec3 eye = cam_->eye();

    RenderFrameParams p;
    p.view = viewM.m;
    p.proj = projM.m;
    p.cameraPos[0] = eye.x;
    p.cameraPos[1] = eye.y;
    p.cameraPos[2] = eye.z;
    p.mode = mode_;
    // Don't outline a hidden selection.
    const bool selHidden =
        selMeshIndex_ >= 0 &&
        static_cast<size_t>(selMeshIndex_) < meshVisible_.size() &&
        !meshVisible_[static_cast<size_t>(selMeshIndex_)];
    p.highlightMeshIndex = selHidden ? -1 : selMeshIndex_;
    for (int i = 0; i < 4; ++i) p.clearColor[i] = clearColor_[i];
    if (!meshVisible_.empty()) {
      p.meshVisible = meshVisible_.data();
      p.meshVisibleCount = static_cast<int>(meshVisible_.size());
    }
    buildHelpers();
    p.helperLines = helperLines_.empty() ? nullptr : helperLines_.data();
    p.helperLineVertexCount = static_cast<int>(helperLines_.size());
    p.overlayLines = overlayLines_.empty() ? nullptr : overlayLines_.data();
    p.overlayLineVertexCount = static_cast<int>(overlayLines_.size());
    renderer_->renderFrame(p);

    const ImTextureID tex = static_cast<ImTextureID>(renderer_->viewportTexture());
    const bool flip = renderer_->caps().flipViewportV;
    const ImVec2 uv0 = flip ? ImVec2(0, 1) : ImVec2(0, 0);
    const ImVec2 uv1 = flip ? ImVec2(1, 0) : ImVec2(1, 1);
    ImGui::Image(tex, avail, uv0, uv1);
    vpHovered_ = ImGui::IsItemHovered();

    // Click-to-pick: a plain left click (no Alt-navigation in progress) selects
    // the nearest mesh under the cursor.
    {
      ImGuiIO& io = ImGui::GetIO();
      if (vpHovered_ && navMode_ == 0 && !io.KeyAlt &&
          ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 rmin = ImGui::GetItemRectMin();
        const ImVec2 mp = io.MousePos;
        const float px = mp.x - rmin.x;
        const float py = mp.y - rmin.y;
        const int hit = pickMesh(px, py, w, h);
        if (hit >= 0 && static_cast<size_t>(hit) < draw_->meshes.size()) {
          selectByPath(draw_->meshes[static_cast<size_t>(hit)].absPath, hit);
        } else {
          clearSelection();  // clicked empty space -> deselect
        }
      }
    }

    if (draw_ && draw_->empty()) {
      // Center the message in the viewport (a fixed pixel offset clips under the
      // tab bar at HiDPI scale).
      const char* msg = (loaded_ && loaded_->ok)
                            ? "No renderable meshes in this scene."
                            : "No scene loaded. Use File > Open...";
      const ImVec2 ts = ImGui::CalcTextSize(msg);
      ImGui::SetCursorPos(ImVec2((avail.x - ts.x) * 0.5f, (avail.y - ts.y) * 0.5f));
      ImGui::TextColored(ImVec4(1, 1, 1, 0.7f), "%s", msg);
    }
  } else {
    vpHovered_ = false;
  }
  ImGui::End();
}

void Gui::buildHelpers() {
  helperLines_.clear();
  overlayLines_.clear();
  const bool zUp = loaded_ && loaded_->ok && loaded_->render.meta.upAxis == "Z";

  auto addLine = [&](float ax, float ay, float az, float bx, float by, float bz,
                     float r, float g, float b) {
    helperLines_.push_back(HelperVertex{{ax, ay, az}, {r, g, b}});
    helperLines_.push_back(HelperVertex{{bx, by, bz}, {r, g, b}});
  };
  auto addOverlay = [&](float ax, float ay, float az, float bx, float by, float bz,
                        float r, float g, float b) {
    overlayLines_.push_back(HelperVertex{{ax, ay, az}, {r, g, b}});
    overlayLines_.push_back(HelperVertex{{bx, by, bz}, {r, g, b}});
  };
  auto addBox = [&](const float mn[3], const float mx[3], float r, float g, float b) {
    const float xs[2] = {mn[0], mx[0]}, ys[2] = {mn[1], mx[1]}, zs[2] = {mn[2], mx[2]};
    for (int yi = 0; yi < 2; ++yi)
      for (int zi = 0; zi < 2; ++zi)
        addLine(xs[0], ys[yi], zs[zi], xs[1], ys[yi], zs[zi], r, g, b);
    for (int xi = 0; xi < 2; ++xi)
      for (int zi = 0; zi < 2; ++zi)
        addLine(xs[xi], ys[0], zs[zi], xs[xi], ys[1], zs[zi], r, g, b);
    for (int xi = 0; xi < 2; ++xi)
      for (int yi = 0; yi < 2; ++yi)
        addLine(xs[xi], ys[yi], zs[0], xs[xi], ys[yi], zs[1], r, g, b);
  };

  // Grid/axes extent from scene bounds.
  float half = 10.0f;
  if (draw_ && draw_->hasBounds) {
    float ex = 0.0f;
    for (int i = 0; i < 3; ++i)
      ex = std::max(ex, draw_->aabbMax[i] - draw_->aabbMin[i]);
    if (ex > 1e-3f) half = std::max(1.0f, std::ceil(ex));
  }

  if (showGrid_) {
    const int n = 10;
    const float step = half / static_cast<float>(n);
    const float gc = 0.32f;
    for (int i = -n; i <= n; ++i) {
      const float t = static_cast<float>(i) * step;
      if (zUp) {  // ground = XY plane (z = 0)
        addLine(-half, t, 0, half, t, 0, gc, gc, gc);
        addLine(t, -half, 0, t, half, 0, gc, gc, gc);
      } else {  // ground = XZ plane (y = 0)
        addLine(-half, 0, t, half, 0, t, gc, gc, gc);
        addLine(t, 0, -half, t, 0, half, gc, gc, gc);
      }
    }
  }
  if (showAxes_) {
    const float L = half;
    addLine(0, 0, 0, L, 0, 0, 0.90f, 0.20f, 0.20f);  // X red
    addLine(0, 0, 0, 0, L, 0, 0.20f, 0.85f, 0.25f);  // Y green
    addLine(0, 0, 0, 0, 0, L, 0.30f, 0.50f, 1.00f);  // Z blue
  }
  if (showSceneBbox_ && draw_ && draw_->hasBounds) {
    addBox(draw_->aabbMin, draw_->aabbMax, 0.90f, 0.90f, 0.30f);
  }
  if (showPrimBbox_ && draw_ && selMeshIndex_ >= 0 &&
      static_cast<size_t>(selMeshIndex_) < draw_->meshes.size()) {
    const auto& m = draw_->meshes[static_cast<size_t>(selMeshIndex_)];
    addBox(m.aabbMin, m.aabbMax, 1.00f, 0.60f, 0.10f);
  }

  // UsdSkel joint hierarchy as bone line segments + a small cross per joint.
  // bind_transforms hold each joint's bind pose in the skeleton's own (model)
  // space (translation at m[3][0..2], row-major). The skinned mesh's world
  // matrix carries the scene placement + up-axis conversion, so we transform the
  // joint positions by it to align the bones with the rendered mesh.
  if (showSkeleton_ && loaded_ && loaded_->ok) {
    // World matrix of the mesh skinned by skeleton `si` (identity if none).
    auto skelWorld = [&](size_t si) -> light3d::Mat4 {
      for (const auto& rm : loaded_->render.meshes) {
        if (rm.skel_id != static_cast<int>(si)) continue;
        if (!draw_) break;
        for (const auto& dm : draw_->meshes) {
          if (dm.absPath == rm.abs_path) {
            light3d::Mat4 W;
            for (int k = 0; k < 16; ++k) W.m[k] = dm.world[k];
            return W;
          }
        }
      }
      return light3d::Mat4::identity();
    };

    const auto& skels = loaded_->render.skeletons;
    for (size_t si = 0; si < skels.size(); ++si) {
      const auto& skel = skels[si];
      const size_t nj = skel.num_joints();
      if (nj == 0 || skel.bind_transforms.size() != nj ||
          skel.parent_joint_indices.size() != nj) {
        continue;  // empty or malformed topology; skip
      }
      const light3d::Mat4 W = skelWorld(si);

      // World-space joint positions + this skeleton's own AABB.
      std::vector<light3d::Vec3> jp(nj);
      light3d::Vec3 mn{1e30f, 1e30f, 1e30f};
      light3d::Vec3 mx{-1e30f, -1e30f, -1e30f};
      for (size_t i = 0; i < nj; ++i) {
        const auto& m = skel.bind_transforms[i].m;  // double m[4][4], row-major
        jp[i] = light3d::transformPoint(
            W, {static_cast<float>(m[3][0]), static_cast<float>(m[3][1]),
                static_cast<float>(m[3][2])});
        mn.x = std::min(mn.x, jp[i].x); mn.y = std::min(mn.y, jp[i].y);
        mn.z = std::min(mn.z, jp[i].z);
        mx.x = std::max(mx.x, jp[i].x); mx.y = std::max(mx.y, jp[i].y);
        mx.z = std::max(mx.z, jp[i].z);
      }
      // Joint-cross size scaled to THIS skeleton's extent (not the whole scene)
      // so crosses stay proportional across characters of different scales.
      const float cs = std::max(light3d::length(mx - mn) * 0.02f, 1e-5f);

      for (size_t i = 0; i < nj; ++i) {
        const light3d::Vec3& p = jp[i];
        // Small axis cross so leaf/isolated joints are visible too.
        addOverlay(p.x - cs, p.y, p.z, p.x + cs, p.y, p.z, 0.20f, 0.95f, 0.95f);
        addOverlay(p.x, p.y - cs, p.z, p.x, p.y + cs, p.z, 0.20f, 0.95f, 0.95f);
        addOverlay(p.x, p.y, p.z - cs, p.x, p.y, p.z + cs, 0.20f, 0.95f, 0.95f);
        const int par = skel.parent_joint_indices[i];
        if (par >= 0 && static_cast<size_t>(par) < nj) {
          const light3d::Vec3& pp = jp[static_cast<size_t>(par)];
          addOverlay(pp.x, pp.y, pp.z, p.x, p.y, p.z, 0.10f, 0.80f, 0.90f);
        }
      }
    }
  }
}

void Gui::drawStageMeta() {
  ImGui::Begin("Stage");
  if (loaded_ && loaded_->ok) {
    const auto& meta = loaded_->render.meta;
    const auto& smeta = loaded_->stage.metas();
    metaFilter_.Draw("Search##meta", -1.0f);  // key / value
    if (ImGui::BeginTable("##stagemeta", 2,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed,
                              ImGui::GetFontSize() * 9.0f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      auto row = [&](const char* k, const std::string& v) {
        if (metaFilter_.IsActive() && !metaFilter_.PassFilter(k) &&
            !metaFilter_.PassFilter(v.c_str())) {
          return;  // filtered out
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(k);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", v.c_str());
      };
      row("upAxis", meta.upAxis);
      row("metersPerUnit", std::to_string(meta.metersPerUnit));
      row("framesPerSecond", std::to_string(meta.framesPerSecond));
      row("timeCodesPerSecond", std::to_string(meta.timeCodesPerSecond));
      if (meta.startTimeCode.has_value())
        row("startTimeCode", std::to_string(*meta.startTimeCode));
      if (meta.endTimeCode.has_value())
        row("endTimeCode", std::to_string(*meta.endTimeCode));
      if (!smeta.defaultPrim.str().empty()) row("defaultPrim", smeta.defaultPrim.str());
      if (!meta.copyright.empty()) row("copyright", meta.copyright);
      if (!meta.comment.empty()) row("comment", meta.comment);
      if (!smeta.doc.value.empty()) row("documentation", smeta.doc.value);
      if (!smeta.subLayers.empty()) {
        std::string s;
        for (const auto& sl : smeta.subLayers) {
          if (!s.empty()) s += "\n";
          s += sl.assetPath.GetAssetPath();
        }
        row("subLayers", s);
      }
      if (!smeta.customLayerData.empty()) {
        std::string s;
        for (const auto& kv : smeta.customLayerData) {
          if (!s.empty()) s += ", ";
          s += kv.first;
        }
        row("customLayerData", s);
      }
      ImGui::EndTable();
    }
  } else {
    HintWrapped("No stage loaded.");
  }
  ImGui::End();
}

}  // namespace tusdview

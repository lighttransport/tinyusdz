// SPDX-License-Identifier: Apache-2.0
#include "gui.hh"

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

}  // namespace

void Gui::setScene(const LoadedScene* loaded, const DrawScene* draw) {
  loaded_ = loaded;
  draw_ = draw;
  selPrim_ = nullptr;
  selPath_.clear();
  selMeshIndex_ = -1;
}

void Gui::frame(Renderer* renderer, OrbitCamera* camera) {
  renderer_ = renderer;
  cam_ = camera;
  drawDockspaceAndMenu();
  drawHierarchy();
  drawInspector();
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

  ImGui::DockBuilderDockWindow("Hierarchy", left);
  ImGui::DockBuilderDockWindow("Stats", left);
  ImGui::DockBuilderDockWindow("Inspector", right);
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
      if (ImGui::MenuItem("Fit to scene", "F", false, draw_ && draw_->hasBounds)) {
        cam_->fitToScene(draw_->aabbMin, draw_->aabbMax);
      }
      ImGui::Checkbox("Show RenderScene nodes", &showRenderNodes_);
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

void Gui::drawPrimTree(const tinyusdz::Prim& prim) {
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
}

void Gui::drawNodeTree(const tydra::Node& node) {
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
}

void Gui::drawHierarchy() {
  ImGui::Begin("Hierarchy");
  if (loaded_ && loaded_->ok) {
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
    ImGui::Separator();

    std::vector<std::string> names;
    std::string err;
    if (tydra::GetPropertyNames(*selPrim_, &names, &err)) {
      if (ImGui::BeginTable("##props", 2,
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const std::string& name : names) {
          tinyusdz::Property prop;
          std::string perr;
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(name.c_str());
          ImGui::TableSetColumnIndex(1);
          if (tydra::GetProperty(*selPrim_, name, &prop, &perr)) {
            ImGui::TextWrapped("%s", PropertyToString(prop).c_str());
          } else {
            ImGui::TextDisabled("<error>");
          }
        }
        ImGui::EndTable();
      }
    } else {
      ImGui::TextDisabled("No properties.");
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

  if (vpHovered_ && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) {
    if (draw_ && draw_->hasBounds) cam_->fitToScene(draw_->aabbMin, draw_->aabbMax);
  }
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
    p.highlightMeshIndex = selMeshIndex_;
    for (int i = 0; i < 4; ++i) p.clearColor[i] = clearColor_[i];
    renderer_->renderFrame(p);

    const ImTextureID tex = static_cast<ImTextureID>(renderer_->viewportTexture());
    const bool flip = renderer_->caps().flipViewportV;
    const ImVec2 uv0 = flip ? ImVec2(0, 1) : ImVec2(0, 0);
    const ImVec2 uv1 = flip ? ImVec2(1, 0) : ImVec2(1, 1);
    ImGui::Image(tex, avail, uv0, uv1);
    vpHovered_ = ImGui::IsItemHovered();

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

}  // namespace tusdview

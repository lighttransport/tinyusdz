#include <iostream>
#include "tinyusdz.hh"

int main() {
  using namespace tinyusdz;

  Stage stage;

  // /Materials scope
  Scope s; s.name = "Materials";
  Prim matScope(s);
  stage.add_root_prim(Prim(s));
  stage.commit();

  // Material "M"
  Material m; m.name = "M";
  Prim matPrim(m);

  // Shader "Principled_BSDF" (UsdPreviewSurface)
  Shader pbr; pbr.name = "Principled_BSDF"; pbr.info_id = kUsdPreviewSurface;
  UsdPreviewSurface surf;
  surf.outputsSurface.set_authored(true);
  // IMPORTANT: do NOT author a default diffuseColor when connecting
  pbr.value = surf;
  Prim pbrPrim(pbr);
  matPrim.add_child(std::move(pbrPrim), /*rename=*/true, nullptr);

  // Shader "Image_Texture" (UsdUVTexture)
  Shader tex; tex.name = "Image_Texture"; tex.info_id = kUsdUVTexture;
  UsdUVTexture uvtex;
  uvtex.outputsRgb.set_authored(true);
  uvtex.file = value::AssetPath("./textures/bk.png");
  tex.value = uvtex;
  Prim texPrim(tex);
  matPrim.add_child(std::move(texPrim), /*rename=*/true, nullptr);

  // Shader "uvmap" (UsdPrimvarReader_float2)
  Shader uv; uv.name = "uvmap"; uv.info_id = kUsdPrimvarReader_float2;
  UsdPrimvarReader_float2 stnode;
  stnode.result.set_authored(true);
  stnode.varname.set_value(std::string("st"));
  uv.value = stnode;
  Prim uvPrim(uv);
  matPrim.add_child(std::move(uvPrim), /*rename=*/true, nullptr);

  // Put Material under /Materials and commit to stabilize paths
  // (re-fetch scope since we moved it into Stage)
  Prim *materials = nullptr;
  for (auto &r : stage.root_prims()) if (r.element_name() == "Materials") materials = &r;
  materials->add_child(std::move(matPrim), /*rename=*/true, nullptr);
  stage.commit();

  // Resolve children
  Prim &addedMat = materials->children().back();
  Prim *pPrincipled=nullptr, *pTex=nullptr, *pUvmap=nullptr;
  for (auto &ch : addedMat.children()) {
    if (const Shader* s = ch.as<Shader>()) {
      if (s->info_id == kUsdPreviewSurface)       pPrincipled = &ch;
      else if (s->info_id == kUsdUVTexture)       pTex        = &ch;
      else if (s->info_id == kUsdPrimvarReader_float2) pUvmap  = &ch;
    }
  }

  // Hook Material.outputs:surface -> Principled_BSDF.outputs:surface
  if (pPrincipled) {
    Material mm = *addedMat.as<Material>();
    mm.surface.set(Path(pPrincipled->absolute_path().full_path_name(), "outputs:surface"));
    addedMat.set_primdata(addedMat.element_name(), mm);
  }

  // inputs:st.connect (works)
  if (pTex && pUvmap) {
    Shader texS = *pTex->as<Shader>();
    Relationship r; r.set(Path(pUvmap->absolute_path().full_path_name(), "outputs:result"));
    texS.props["inputs:st.connect"] = Property(r);
    pTex->set_primdata(pTex->element_name(), texS);
  }

  // inputs:diffuseColor.connect (DOES NOT SERIALIZE)

  // A) Typed-attribute way (FIXED - need both set_connection AND set_value_empty)
  if (pPrincipled && pTex) {
    Shader pv = *pPrincipled->as<Shader>();
    if (auto *node = pv.value.as<UsdPreviewSurface>()) {
      node->diffuseColor.set_connection(
            Path(pTex->absolute_path().full_path_name(), "outputs:rgb"));
      node->diffuseColor.set_value_empty(); // CRITICAL: Required for proper connection
      pv.value = *node;
      pPrincipled->set_primdata(pPrincipled->element_name(), pv);
    }
  }

  // // B) Props way (also does not serialize for UsdPreviewSurface)
  // if (pPrincipled && pTex) {
  //   Shader pv = *pPrincipled->as<Shader>();
  //   Relationship r; r.set(Path(pTex->absolute_path().full_path_name(), "outputs:rgb"));
  //   pv.props["inputs:diffuseColor.connect"] = Property(r);
  //   pPrincipled->set_primdata(pPrincipled->element_name(), pv);
  // }

  stage.commit();

  std::string warn, err;
  usda::SaveAsUSDA("repro.usda", stage, &warn, &err);
  if (!warn.empty()) std::cout << "WARN: " << warn << "\n";
  if (!err.empty())  std::cout << "ERR : " << err << "\n";

  return 0;
}
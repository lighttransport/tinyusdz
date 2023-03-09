import ctinyusdz

stage = ctinyusdz.load_usd("../../models/texture-cat-plane.usda")

print(stage)
print(stage.metas)
print(stage.metas().metersPerUnit)
stage.metas().metersPerUnit = 3.0
# FIXME. metersPerUnit does not update correctly.
print(stage.metas().metersPerUnit)

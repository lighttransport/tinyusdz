#include "prim-reconstruct.hh"

#include "str-util.hh"

#include "usdGeom.hh"

#include "common-macros.inc"

// For PUSH_ERROR_AND_RETURN
#define PushError(s) if (err) { (*err) += s; }
   

namespace tinyusdz {
namespace prim {

bool ReconstructXformOpsFromProperties(
  std::set<std::string> &table, /* inout */
  const std::map<std::string, Property> &properties,
  std::vector<XformOp> *xformOps,
  std::string *err) 
{

  constexpr auto kTranslate = "xformOp:translate";
  constexpr auto kTransform = "xformOp:transform";
  constexpr auto kScale = "xformOp:scale";
  constexpr auto kRotateX = "xformOp:rotateX";
  constexpr auto kRotateY = "xformOp:rotateY";
  constexpr auto kRotateZ = "xformOp:rotateZ";
  constexpr auto kRotateXYZ = "xformOp:rotateXYZ";
  constexpr auto kRotateXZY = "xformOp:rotateXZY";
  constexpr auto kRotateYXZ = "xformOp:rotateYXZ";
  constexpr auto kRotateYZX = "xformOp:rotateYZX";
  constexpr auto kRotateZXY = "xformOp:rotateZXY";
  constexpr auto kRotateZYX = "xformOp:rotateZYX";
  constexpr auto kOrient = "xformOp:orient";

  // false : no prefix found.
  // true : return suffix(first namespace ':' is ommited.).
  // - "" for prefix only "xformOp:translate"
  // - "blender:pivot" for "xformOp:translate:blender:pivot"
  auto SplitXformOpToken =
      [](const std::string &s,
         const std::string &prefix) -> nonstd::optional<std::string> {
    if (startsWith(s, prefix)) {
      if (s.compare(prefix) == 0) {
        // prefix only.
        return std::string();  // empty suffix
      } else {
        std::string suffix = removePrefix(s, prefix);
        DCOUT("suffix = " << suffix);
        if (suffix.length() == 1) {  // maybe namespace only.
          return nonstd::nullopt;
        }

        // remove namespace ':'
        if (suffix[0] == ':') {
          // ok
          suffix.erase(0, 1);
        } else {
          return nonstd::nullopt;
        }

        return std::move(suffix);
      }
    }

    return nonstd::nullopt;
  };


  // Lookup xform values from `xformOpOrder`
  // TODO: TimeSamples, Connection
  if (properties.count("xformOpOrder")) {
    // array of string
    auto prop = properties.at("xformOpOrder");

    if (prop.IsRel()) {
      PUSH_ERROR_AND_RETURN("Relation for `xformOpOrder` is not supported.");
    } else if (auto pv =
                   prop.attrib.var.get_value<std::vector<value::token>>()) {

      // 'uniform' check
      if (prop.attrib.variability != Variability::Uniform) {
        PUSH_ERROR_AND_RETURN("`xformOpOrder` must have `uniform` variability.");
      }

      for (size_t i = 0; i < pv.value().size(); i++) {
        const auto &item = pv.value()[i];

        XformOp op;

        std::string tok = item.str();
        DCOUT("xformOp token = " << tok);

        if (startsWith(tok, "!resetXformStack!")) {
          if (tok.compare("!resetXformStack!") != 0) {
            PUSH_ERROR_AND_RETURN(
                "`!resetXformStack!` must be defined solely(not to be a prefix "
                "to \"xformOp:*\")");
          }

          if (i != 0) {
            PUSH_ERROR_AND_RETURN(
                "`!resetXformStack!` must appear at the first element of "
                "xformOpOrder list.");
          }

          op.op = XformOp::OpType::ResetXformStack;
          xformOps->emplace_back(op);

          // skip looking up property
          continue;
        }

        if (startsWith(tok, "!invert!")) {
          DCOUT("invert!");
          op.inverted = true;
          tok = removePrefix(tok, "!invert!");
          DCOUT("tok = " << tok);
        }

        auto it = properties.find(tok);
        if (it == properties.end()) {
          PUSH_ERROR_AND_RETURN("Property `" + tok + "` not found.");
        }
        if (it->second.IsConnection()) {
          PUSH_ERROR_AND_RETURN(
              "Connection(.connect) of xformOp property is not yet supported: "
              "`" +
              tok + "`");
        }
        const PrimAttrib &attr = it->second.attrib;

        // Check `xformOp` namespace
        if (auto xfm = SplitXformOpToken(tok, kTransform)) {
          op.op = XformOp::OpType::Transform;
          op.suffix = xfm.value();  // may contain nested namespaces

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::matrix4d>()) {
            op.set_scalar(pvd.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:transform` must be type `matrix4d`, but got type `" +
                attr.var.type_name() + "`.");
          }

        } else if (auto tx = SplitXformOpToken(tok, kTranslate)) {
          op.op = XformOp::OpType::Translate;
          op.suffix = tx.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:translate` must be type `double3` or `float3`, but "
                "got type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto scale = SplitXformOpToken(tok, kScale)) {
          op.op = XformOp::OpType::Scale;
          op.suffix = scale.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:scale` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotX = SplitXformOpToken(tok, kRotateX)) {
          op.op = XformOp::OpType::RotateX;
          op.suffix = rotX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<double>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<float>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateX` must be type `double` or `float`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotY = SplitXformOpToken(tok, kRotateY)) {
          op.op = XformOp::OpType::RotateY;
          op.suffix = rotX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<double>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<float>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateY` must be type `double` or `float`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotZ = SplitXformOpToken(tok, kRotateZ)) {
          op.op = XformOp::OpType::RotateY;
          op.suffix = rotZ.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<double>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<float>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateZ` must be type `double` or `float`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateXYZ = SplitXformOpToken(tok, kRotateXYZ)) {
          op.op = XformOp::OpType::RotateXYZ;
          op.suffix = rotateXYZ.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateXYZ` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateXZY = SplitXformOpToken(tok, kRotateXZY)) {
          op.op = XformOp::OpType::RotateXZY;
          op.suffix = rotateXZY.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateXZY` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateYXZ = SplitXformOpToken(tok, kRotateYXZ)) {
          op.op = XformOp::OpType::RotateYXZ;
          op.suffix = rotateYXZ.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateYXZ` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateYZX = SplitXformOpToken(tok, kRotateYZX)) {
          op.op = XformOp::OpType::RotateYZX;
          op.suffix = rotateYZX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateYZX` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateZXY = SplitXformOpToken(tok, kRotateZXY)) {
          op.op = XformOp::OpType::RotateZXY;
          op.suffix = rotateZXY.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateZXY` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateZYX = SplitXformOpToken(tok, kRotateZYX)) {
          op.op = XformOp::OpType::RotateZYX;
          op.suffix = rotateZYX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateZYX` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto orient = SplitXformOpToken(tok, kOrient)) {
          op.op = XformOp::OpType::Orient;
          op.suffix = orient.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::quatf>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::quatd>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:orient` must be type `quatf` or `quatd`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "token for xformOpOrder must have namespace `xformOp:***`, or .");
        }

        xformOps->emplace_back(op);
        table.insert(tok);
      }

    } else {
      PUSH_ERROR_AND_RETURN(
          "`xformOpOrder` must be type `token[]` but got type `"
          << prop.attrib.var.type_name() << "`.");
    }
  }

  return true;
}

template <>
bool ReconstructPrim(
    const PropertyMap &properties,
    const ReferenceList &references,
    Xform *xform,
    std::string *err) {

  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }

  std::set<std::string> table;
  if (!prim::ReconstructXformOpsFromProperties(table, properties, &xform->xformOps, err)) {
    return false;
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
    }
  }

  return true;
}


} // namespace prim
} // namespace tinyusdz

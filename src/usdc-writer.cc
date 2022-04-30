#include "usdc-writer.hh"
//#include "pprinter.hh"

#include <fstream>
#include <iostream>
#include <sstream>

namespace tinyusdz {
namespace usdc {

namespace {


class Writer {
 public:
  Writer(const Scene &scene) : _scene(scene) {}

  const Scene &_scene;

  const std::string &Error() const { return _err; }

 private:
  Writer() = delete;
  Writer(const Writer &) = delete;

  std::string _err;
};

}  // namespace

bool SaveAsUSDC(const std::string &filename, const Scene &scene,
                std::string *warn, std::string *err) {
  (void)filename;
  (void)warn;

  // TODO
  Writer writer(scene);

  if (err) {
    (*err) += "USDC writer is not yet implemented.\n";
  }

  return false;
}

} // namespace usdc
}  // namespace tinyusdz

#include <emscripten/bind.h>

#include <vector>

using namespace emscripten;

///
/// Simple C++ wrapper class for Emscripten
///
class MyLoader {
 public:
  MyLoader(const std::string &binary) {
    binary_ = binary;
  }
  ~MyLoader() {}

  bool ok() const { return binary_.size(); }

  const std::string error() const { return error_; }

 private:
  std::string binary_;
  std::string warn_;
  std::string error_;

};

// Register STL
EMSCRIPTEN_BINDINGS(stl_wrappters) {
  register_vector<float>("VectorFloat");
  register_vector<int>("VectorInt");
  register_vector<uint32_t>("VectorUInt");
}

EMSCRIPTEN_BINDINGS(myloader_module) {
  class_<MyLoader>("MyLoader")
      .constructor<const std::string &>()
      .function("ok", &MyLoader::ok)
      .function("error", &MyLoader::error);
}

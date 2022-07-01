#if defined(__wasi__)
#else
#include <thread>
#endif

#include "crate-format.hh"

//#include "integerCoding.h"
//#include "primvar.hh"
//#include "tinyusdz.hh"
//#include "pprinter.hh"
//
//#include "lz4-compression.hh"
//#include "stream-reader.hh"

//#define PUSH_ERROR(s) { \
//  std::ostringstream ss; \
//  ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
//  ss << s; \
//  _err += ss.str() + "\n"; \
//} while (0)

#if 0
#define PUSH_WARN(s) { \
  std::ostringstream ss; \
  ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
  ss << s; \
  _warn += ss.str() + "\n"; \
} while (0)
#endif

namespace tinyusdz {
namespace crate {





} // namespace crate
} // namesapce tinyusdz

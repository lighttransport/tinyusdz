#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-logger.h"
#include "logger.hh"

void logger_test(void) {
  using namespace tinyusdz::logging;

  // -----------------------------------------------------------------------
  // Verify that the LogLevel enum values are ordered by increasing severity.
  // -----------------------------------------------------------------------
  TEST_CHECK(static_cast<int>(LogLevel::Debug)    == 0);
  TEST_CHECK(static_cast<int>(LogLevel::Info)     == 1);
  TEST_CHECK(static_cast<int>(LogLevel::Warn)     == 2);
  TEST_CHECK(static_cast<int>(LogLevel::Error)    == 3);
  TEST_CHECK(static_cast<int>(LogLevel::Critical) == 4);
  TEST_CHECK(static_cast<int>(LogLevel::Off)      == 5);

  // Debug < Info < Warn < Error < Critical
  TEST_CHECK(static_cast<int>(LogLevel::Debug)    < static_cast<int>(LogLevel::Info));
  TEST_CHECK(static_cast<int>(LogLevel::Info)     < static_cast<int>(LogLevel::Warn));
  TEST_CHECK(static_cast<int>(LogLevel::Warn)     < static_cast<int>(LogLevel::Error));
  TEST_CHECK(static_cast<int>(LogLevel::Error)    < static_cast<int>(LogLevel::Critical));
  TEST_CHECK(static_cast<int>(LogLevel::Critical) < static_cast<int>(LogLevel::Off));

  // -----------------------------------------------------------------------
  // Verify shouldLog() with default threshold (Warn).
  // -----------------------------------------------------------------------
  Logger &logger = Logger::getInstance();
  logger.setLogLevel(LogLevel::Warn);

  // Messages below Warn threshold must be suppressed.
  TEST_CHECK(!logger.shouldLog(LogLevel::Debug));
  TEST_CHECK(!logger.shouldLog(LogLevel::Info));

  // Messages at or above Warn threshold must be allowed.
  TEST_CHECK(logger.shouldLog(LogLevel::Warn));
  TEST_CHECK(logger.shouldLog(LogLevel::Error));
  TEST_CHECK(logger.shouldLog(LogLevel::Critical));

  // -----------------------------------------------------------------------
  // Verify shouldLog() with Info threshold.
  // -----------------------------------------------------------------------
  logger.setLogLevel(LogLevel::Info);

  TEST_CHECK(!logger.shouldLog(LogLevel::Debug));
  TEST_CHECK(logger.shouldLog(LogLevel::Info));
  TEST_CHECK(logger.shouldLog(LogLevel::Warn));
  TEST_CHECK(logger.shouldLog(LogLevel::Error));
  TEST_CHECK(logger.shouldLog(LogLevel::Critical));

  // -----------------------------------------------------------------------
  // Verify shouldLog() with Debug threshold (all messages pass).
  // -----------------------------------------------------------------------
  logger.setLogLevel(LogLevel::Debug);

  TEST_CHECK(logger.shouldLog(LogLevel::Debug));
  TEST_CHECK(logger.shouldLog(LogLevel::Info));
  TEST_CHECK(logger.shouldLog(LogLevel::Warn));
  TEST_CHECK(logger.shouldLog(LogLevel::Error));
  TEST_CHECK(logger.shouldLog(LogLevel::Critical));

  // -----------------------------------------------------------------------
  // Verify shouldLog() with Off threshold (all messages suppressed).
  // -----------------------------------------------------------------------
  logger.setLogLevel(LogLevel::Off);

  TEST_CHECK(!logger.shouldLog(LogLevel::Debug));
  TEST_CHECK(!logger.shouldLog(LogLevel::Info));
  TEST_CHECK(!logger.shouldLog(LogLevel::Warn));
  TEST_CHECK(!logger.shouldLog(LogLevel::Error));
  TEST_CHECK(!logger.shouldLog(LogLevel::Critical));

  // Reset to default for other tests.
  logger.setLogLevel(LogLevel::Warn);
}

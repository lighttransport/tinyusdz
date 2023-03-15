#include <stdio.h>
#include <stdlib.h>

#include "c-tinyusd.h"

int main(int argc, char **argv) {

  if (argc < 2) {
    printf("Need input.usd/usda/usdc/usdz\n");
    return EXIT_FAILURE;
  }

  int ret = c_tinyusd_is_usd_file(argv[1]);
  if (!ret) {
    printf("%s is not found or not a valid USD file.\n", argv[1]);
  }

  CTinyUSDStage stage;
  c_tinyusd_stage_new(&stage);

  c_tinyusd_string warn;
  c_tinyusd_string_new_empty(&warn);

  c_tinyusd_string err;
  c_tinyusd_string_new_empty(&err);

  ret = c_tinyusd_load_usd_from_file(argv[1], &stage, &warn, &err);

  c_tinyusd_stage_free(&stage);
  c_tinyusd_string_free(&warn);
  c_tinyusd_string_free(&err);

  return EXIT_SUCCESS;
}

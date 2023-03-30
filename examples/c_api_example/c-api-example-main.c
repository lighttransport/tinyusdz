#include <stdio.h>
#include <stdlib.h>

#include "c-tinyusd.h"

// Return 1: Continue traversal. 0: Terminate traversal.
int prim_traverse_fun(const CTinyUSDPrim *prim, const CTinyUSDPath *path) {
  if (!prim) {
    return 1;
  }

  if (!path) {
    return 1;
  }

  printf("prim trav...\n");

  return 1;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Need input.usd/usda/usdc/usdz\n");
    return EXIT_FAILURE;
  }

  int ret = c_tinyusd_is_usd_file(argv[1]);
  if (!ret) {
    printf("%s is not found or not a valid USD file.\n", argv[1]);
    return EXIT_FAILURE;
  }

  CTinyUSDStage *stage = c_tinyusd_stage_new();

  c_tinyusd_string_t *warn = c_tinyusd_string_new_empty();
  c_tinyusd_string_t *err = c_tinyusd_string_new_empty();

  ret = c_tinyusd_load_usd_from_file(argv[1], stage, warn, err);

  if (c_tinyusd_string_size(warn)) {
    printf("WARN: %s\n", c_tinyusd_string_str(warn));
  }

  if (!ret) {
    if (c_tinyusd_string_size(err)) {
      printf("ERR: %s\n", c_tinyusd_string_str(err));
    }
  }

  // print USD(Stage) content as ASCII.
  c_tinyusd_string_t *str = c_tinyusd_string_new_empty();

  if (!c_tinyusd_stage_to_string(stage, str)) {
    printf("Unexpected error when exporting Stage to string.\n");
    return EXIT_FAILURE;
  }

  printf("%s\n", c_tinyusd_string_str(str));

  printf("-- traverse Prim --\n");
  //
  // Traverse Prims in the Stage.
  //
  if (!c_tinyusd_stage_traverse(stage, prim_traverse_fun, err)) {
    if (c_tinyusd_string_size(err)) {
      printf("Traverse error: %s\n", c_tinyusd_string_str(err));
    }
  }
  printf("-- end traverse Prim --\n");

  //
  // Create new Prim
  //
  // You can also create a Prim with string
  // CTinyUSDPrim *prim = c_tinyusd_prim_new("Xform");
  CTinyUSDPrim *prim = c_tinyusd_prim_new_builtin(C_TINYUSD_PRIM_XFORM);

  if (!prim) {
    printf("Failed to new Xform Prim.\n");
    return EXIT_FAILURE;
  }

  {
    c_tinyusd_token_vector_t *tokv = c_tinyusd_token_vector_new_empty();
    if (!tokv) {
      printf("New token vector failed.\n");
      return EXIT_FAILURE;
    }

    if (!c_tinyusd_prim_get_property_names(prim, tokv)) {
      printf("Failed to get property names from a Prim.\n");
      return EXIT_FAILURE;
    }

    if (!c_tinyusd_token_vector_free(tokv)) {
      printf("Freeing token vector failed.\n");
      return EXIT_FAILURE;
    }
  }

  {
    CTinyUSDAttributeValue attr_value = {0};
    if (!c_tinyusd_attribute_value_new_int(&attr_value, 7)) {
      printf("Failed to new `int` attribute value.\n");
      return EXIT_FAILURE;
    }

    if (!c_tinyusd_attribute_value_to_string(&attr_value, str)) {
      printf("Failed to print `int` attribute value.\n");
      return EXIT_FAILURE;
    }

    printf("Int attribute value: %s\n", c_tinyusd_string_str(str));

    if (!c_tinyusd_attribute_value_free(&attr_value)) {
      printf("AttributeValue free failed.\n");
      return EXIT_FAILURE;
      
    }
  }

  //
  // Release resources.
  //

  if (!c_tinyusd_string_free(str)) {
    printf("str string free failed.\n");
    return EXIT_FAILURE;
  }

  if (!c_tinyusd_prim_free(prim)) {
    printf("Prim free failed.\n");
    return EXIT_FAILURE;
  }

  if (!c_tinyusd_stage_free(stage)) {
    printf("Stage free failed.\n");
    return EXIT_FAILURE;
  }
  if (!c_tinyusd_string_free(warn)) {
    printf("warn string free failed.\n");
    return EXIT_FAILURE;
  }
  if (!c_tinyusd_string_free(err)) {
    printf("err string free failed.\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

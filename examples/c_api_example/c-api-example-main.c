#include <stdio.h>
#include <stdlib.h>

#include "c-lightusd.h"

// Return 1: Continue traversal. 0: Terminate traversal.
int prim_traverse_fun(const CLightUSDPrim *prim, const CLightUSDPath *path) {
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

  if (argc > 1) {

    int ret = c_lightusd_is_usd_file(argv[1]);
    if (!ret) {
      printf("%s is not found or not a valid USD file.\n", argv[1]);
      return EXIT_FAILURE;
    }

    CLightUSDStage *stage = c_lightusd_stage_new();

    c_lightusd_string_t *warn = c_lightusd_string_new_empty();
    c_lightusd_string_t *err = c_lightusd_string_new_empty();

    ret = c_lightusd_load_usd_from_file(argv[1], stage, warn, err);

    if (c_lightusd_string_size(warn)) {
      printf("WARN: %s\n", c_lightusd_string_str(warn));
    }

    if (!ret) {
      if (c_lightusd_string_size(err)) {
        printf("ERR: %s\n", c_lightusd_string_str(err));
      }
    }

    // print USD(Stage) content as ASCII.
    c_lightusd_string_t *str = c_lightusd_string_new_empty();

    if (!c_lightusd_stage_to_string(stage, str)) {
      printf("Unexpected error when exporting Stage to string.\n");
      return EXIT_FAILURE;
    }

    printf("%s\n", c_lightusd_string_str(str));

    printf("-- traverse Prim --\n");
    //
    // Traverse Prims in the Stage.
    //
    if (!c_lightusd_stage_traverse(stage, prim_traverse_fun, err)) {
      if (c_lightusd_string_size(err)) {
        printf("Traverse error: %s\n", c_lightusd_string_str(err));
      }
    }
    printf("-- end traverse Prim --\n");

    //
    // Release resources.
    //

    if (!c_lightusd_string_free(str)) {
      printf("str string free failed.\n");
      return EXIT_FAILURE;
    }

    if (!c_lightusd_stage_free(stage)) {
      printf("Stage free failed.\n");
      return EXIT_FAILURE;
    }
    if (!c_lightusd_string_free(warn)) {
      printf("warn string free failed.\n");
      return EXIT_FAILURE;
    }
    if (!c_lightusd_string_free(err)) {
      printf("err string free failed.\n");
      return EXIT_FAILURE;
    }

  } else {

    c_lightusd_string_t *str = c_lightusd_string_new_empty();
    c_lightusd_string_t *err = c_lightusd_string_new_empty();

    //
    // Create new Prim
    //
    CLightUSDPrim *prim = c_lightusd_prim_new("Xform", err);
    // You can also create a builtin Prim with enum
    //CLightUSDPrim *prim = c_lightusd_prim_new_builtin(C_LIGHTUSD_PRIM_XFORM);
    if (!prim) {
      if (err) {
        printf("Failed to new Prim: error = %s\n", c_lightusd_string_str(err));
      } else {
        printf("Failed to new Prim.\n");
      }
      return EXIT_FAILURE;
    }

    CLightUSDPrim *child_prim = c_lightusd_prim_new_builtin(C_LIGHTUSD_PRIM_MESH);
    if (!child_prim) {
      printf("Failed to new Mesh Prim.\n");
      return EXIT_FAILURE;
    }

    // You can also use c_lightusd_prim_append_child_move() (eqivalent to emplace_back(std::move(child_prim)))
    // In this case, you don't need to call prim free API.
    if (!c_lightusd_prim_append_child(prim, child_prim)) {
      printf("Prim: Append child failed.\n");
      return EXIT_FAILURE;
    }

    if (!c_lightusd_prim_free(child_prim)) {
      printf("Prim: Child Prim free failed.\n");
      return EXIT_FAILURE;
    }

    {
      c_lightusd_token_vector_t *tokv = c_lightusd_token_vector_new_empty();
      if (!tokv) {
        printf("New token vector failed.\n");
        return EXIT_FAILURE;
      }

      if (!c_lightusd_prim_get_property_names(prim, tokv)) {
        printf("Failed to get property names from a Prim.\n");
        return EXIT_FAILURE;
      }

      if (!c_lightusd_token_vector_free(tokv)) {
        printf("Freeing token vector failed.\n");
        return EXIT_FAILURE;
      }
    }

    {
      CLightUSDValue *attr_value = c_lightusd_value_new_int(7);
      if (!attr_value) {
        printf("Failed to new `int` value.\n");
        return EXIT_FAILURE;
      }

      if (!c_lightusd_value_to_string(attr_value, str)) {
        printf("Failed to print `int` value.\n");
        return EXIT_FAILURE;
      }

      printf("Int attribute value: %s\n", c_lightusd_string_str(str));

      printf("Is value numeric?: %d\n", c_lightusd_value_type_is_numeric(c_lightusd_value_type(attr_value)));

      if (!c_lightusd_value_free(attr_value)) {
        printf("Value free failed.\n");
        return EXIT_FAILURE;

      }
    }

    {
      c_lightusd_string_t *strval = c_lightusd_string_new("myval");

      /*
       * NOTE: `token` and `string` value are coped, so need to free it after using it.
       */
      CLightUSDValue *attr_value = c_lightusd_value_new_string(strval);
      if (!attr_value) {
        printf("Failed to new `string` value.\n");
        return EXIT_FAILURE;
      }

      if (!c_lightusd_string_free(strval)) {
        printf("str free failed.\n");
        return EXIT_FAILURE;

      }

      if (!c_lightusd_value_to_string(attr_value, str)) {
        printf("Failed to print `string` value.\n");
        return EXIT_FAILURE;
      }

      printf("String attribute value: %s\n", c_lightusd_string_str(str));

      if (!c_lightusd_value_free(attr_value)) {
        printf("Value free failed.\n");
        return EXIT_FAILURE;

      }
    }

    if (!c_lightusd_prim_free(prim)) {
      printf("Prim free failed.\n");
      return EXIT_FAILURE;
    }

    if (!c_lightusd_string_free(str)) {
      printf("str string free failed.\n");
      return EXIT_FAILURE;
    }

    if (!c_lightusd_string_free(err)) {
      printf("err string free failed.\n");
      return EXIT_FAILURE;
    }

  }


  return EXIT_SUCCESS;
}

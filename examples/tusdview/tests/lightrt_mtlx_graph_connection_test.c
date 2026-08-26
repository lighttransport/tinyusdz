// SPDX-License-Identifier: Apache-2.0
#include "mtlx_doc.h"

#include <stdio.h>
#include <string.h>

static const MtlxInput *find_input(const MtlxNode *node, const char *name) {
  for (int i = 0; i < node->ninput; ++i) {
    if (!strcmp(node->inputs[i].name, name)) return &node->inputs[i];
  }
  return NULL;
}

static int find_any_node(const MtlxDoc *doc, const char *name) {
  int id = mtlx_find_node(doc, -1, name);
  for (int graph = 0; id < 0 && graph < doc->ngraph; ++graph)
    id = mtlx_find_node(doc, graph, name);
  return id;
}

static int require_edge(const MtlxDoc *doc, const char *consumer,
                        const char *input_name, const char *producer,
                        const char *output, const char *channels) {
  const int consumer_id = find_any_node(doc, consumer);
  const int producer_id = find_any_node(doc, producer);
  if (consumer_id < 0 || producer_id < 0) {
    fprintf(stderr, "missing graph node: %s -> %s\n", producer, consumer);
    return 0;
  }
  const MtlxInput *input = find_input(&doc->nodes[consumer_id], input_name);
  if (!input || input->src_node != producer_id ||
      (!!output != !!input->src_output) ||
      (output && strcmp(output, input->src_output)) ||
      (!!channels != !!input->channels) ||
      (channels && strcmp(channels, input->channels))) {
    fprintf(stderr, "incorrect edge: %s.%s <- %s\n", consumer, input_name,
            producer);
    return 0;
  }
  return 1;
}

int main(void) {
  /* The consumer is deliberately authored before its producers. The surface
   * also reaches the graph through nodegraph+output indirection. */
  const char *xml =
      "<materialx version=\"1.39\">"
      " <nodegraph name=\"NG\">"
      "  <multiply name=\"product\" type=\"color3\">"
      "   <input name=\"in1\" type=\"color3\" nodename=\"sum\"/>"
      "   <input name=\"in2\" type=\"float\" nodename=\"gain\"/>"
      "  </multiply>"
      "  <add name=\"sum\" type=\"color3\">"
      "   <input name=\"in1\" type=\"color3\" nodename=\"source\"/>"
      "   <input name=\"in2\" type=\"color3\" value=\"0.1,0.2,0.3\"/>"
      "  </add>"
      "  <constant name=\"gain\" type=\"float\"><input name=\"value\" type=\"float\" value=\"0.5\"/></constant>"
      "  <constant name=\"source\" type=\"color4\"><input name=\"value\" type=\"color4\" value=\"0.2,0.4,0.8,0.7\"/></constant>"
      "  <output name=\"beauty\" type=\"color3\" nodename=\"product\"/>"
      "  <output name=\"alpha\" type=\"float\" nodename=\"source\" output=\"a\"/>"
      " </nodegraph>"
      " <open_pbr_surface name=\"Surface\" type=\"surfaceshader\">"
      "  <input name=\"base_color\" type=\"color3\" nodegraph=\"NG\" output=\"beauty\"/>"
      "  <input name=\"geometry_opacity\" type=\"float\" nodegraph=\"NG\" output=\"alpha\"/>"
      " </open_pbr_surface>"
      " <surfacematerial name=\"Mat\"><input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"Surface\"/></surfacematerial>"
      "</materialx>";
  MtlxDoc *doc = mtlx_load_string(xml);
  if (!doc) {
    fprintf(stderr, "failed to parse graph connection fixture\n");
    return 1;
  }
  int ok = doc->ngraph == 1 && doc->nmat == 1 &&
           require_edge(doc, "product", "in1", "sum", NULL, NULL) &&
           require_edge(doc, "product", "in2", "gain", NULL, NULL) &&
           require_edge(doc, "sum", "in1", "source", NULL, NULL) &&
           require_edge(doc, "Surface", "base_color", "product", NULL, NULL) &&
           require_edge(doc, "Surface", "geometry_opacity", "source", "a",
                        NULL);
  if (doc->mats[0].surface_node != mtlx_find_node(doc, -1, "Surface")) {
    fprintf(stderr, "surface material connection did not resolve\n");
    ok = 0;
  }
  mtlx_free(doc);
  return ok ? 0 : 1;
}

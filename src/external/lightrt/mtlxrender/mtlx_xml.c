#include "mtlx_xml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *s; const char *end; } Cur;

static void skip_ws(Cur *c) {
    while (c->s < c->end && (*c->s == ' ' || *c->s == '\t' || *c->s == '\n' || *c->s == '\r'))
        c->s++;
}

/* Decode the five predefined XML entities into a freshly malloc'd string. */
static char *decode_entities(const char *src, size_t len) {
    char *out = (char *)malloc(len + 1);
    size_t o = 0;
    for (size_t i = 0; i < len;) {
        if (src[i] == '&') {
            if (len - i >= 5 && memcmp(src + i, "&amp;", 5) == 0) { out[o++] = '&'; i += 5; continue; }
            if (len - i >= 4 && memcmp(src + i, "&lt;", 4) == 0) { out[o++] = '<'; i += 4; continue; }
            if (len - i >= 4 && memcmp(src + i, "&gt;", 4) == 0) { out[o++] = '>'; i += 4; continue; }
            if (len - i >= 6 && memcmp(src + i, "&quot;", 6) == 0) { out[o++] = '"'; i += 6; continue; }
            if (len - i >= 6 && memcmp(src + i, "&apos;", 6) == 0) { out[o++] = '\''; i += 6; continue; }
        }
        out[o++] = src[i++];
    }
    out[o] = '\0';
    return out;
}

static char *dup_range(const char *s, size_t len) {
    char *o = (char *)malloc(len + 1);
    memcpy(o, s, len);
    o[len] = '\0';
    return o;
}

static int is_name_char(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '.' || ch == ':';
}

static XmlNode *node_new(char *tag) {
    XmlNode *n = (XmlNode *)calloc(1, sizeof(XmlNode));
    n->tag = tag;
    return n;
}

static void node_add_child(XmlNode *p, XmlNode *c) {
    p->children = (XmlNode *)realloc(p->children, sizeof(XmlNode) * (p->nchild + 1));
    p->children[p->nchild++] = *c;
    free(c);
}

static void node_add_attr(XmlNode *n, char *name, char *value) {
    n->attrs = (XmlAttr *)realloc(n->attrs, sizeof(XmlAttr) * (n->nattr + 1));
    n->attrs[n->nattr].name = name;
    n->attrs[n->nattr].value = value;
    n->nattr++;
}

/* Skip <!-- --> , <?...?> , <!...> . Returns 1 if it consumed one. */
static int skip_misc(Cur *c) {
    if (c->s + 4 <= c->end && memcmp(c->s, "<!--", 4) == 0) {
        const char *p = c->s + 4;
        while (p + 3 <= c->end && memcmp(p, "-->", 3) != 0) p++;
        c->s = (p + 3 <= c->end) ? p + 3 : c->end;
        return 1;
    }
    if (c->s + 2 <= c->end && c->s[0] == '<' && c->s[1] == '?') {
        const char *p = c->s + 2;
        while (p + 2 <= c->end && memcmp(p, "?>", 2) != 0) p++;
        c->s = (p + 2 <= c->end) ? p + 2 : c->end;
        return 1;
    }
    if (c->s + 2 <= c->end && c->s[0] == '<' && c->s[1] == '!') {
        const char *p = c->s + 2;
        while (p < c->end && *p != '>') p++;
        c->s = (p < c->end) ? p + 1 : c->end;
        return 1;
    }
    return 0;
}

/* Parse one element starting at '<'. Returns node or NULL on error. */
static XmlNode *parse_element(Cur *c);

static int parse_children(Cur *c, XmlNode *parent) {
    for (;;) {
        /* skip text/whitespace until next '<' */
        while (c->s < c->end && *c->s != '<') c->s++;
        if (c->s >= c->end) return 0; /* unexpected EOF */
        if (c->s + 1 < c->end && c->s[1] == '/') return 1; /* closing tag for parent */
        if (skip_misc(c)) continue;
        XmlNode *child = parse_element(c);
        if (!child) return 0;
        node_add_child(parent, child);
    }
}

static XmlNode *parse_element(Cur *c) {
    if (c->s >= c->end || *c->s != '<') return NULL;
    c->s++; /* consume '<' */
    const char *ns = c->s;
    while (c->s < c->end && is_name_char(*c->s)) c->s++;
    if (c->s == ns) return NULL;
    XmlNode *n = node_new(dup_range(ns, (size_t)(c->s - ns)));

    /* attributes */
    for (;;) {
        skip_ws(c);
        if (c->s >= c->end) { xml_free(n); return NULL; }
        if (*c->s == '/' || *c->s == '>') break;
        const char *as = c->s;
        while (c->s < c->end && is_name_char(*c->s)) c->s++;
        if (c->s == as) { xml_free(n); return NULL; }
        char *aname = dup_range(as, (size_t)(c->s - as));
        skip_ws(c);
        if (c->s >= c->end || *c->s != '=') { free(aname); xml_free(n); return NULL; }
        c->s++; /* '=' */
        skip_ws(c);
        if (c->s >= c->end || (*c->s != '"' && *c->s != '\'')) { free(aname); xml_free(n); return NULL; }
        char quote = *c->s++;
        const char *vs = c->s;
        while (c->s < c->end && *c->s != quote) c->s++;
        char *aval = decode_entities(vs, (size_t)(c->s - vs));
        if (c->s < c->end) c->s++; /* closing quote */
        node_add_attr(n, aname, aval);
    }

    if (*c->s == '/') { /* self-closing */
        c->s++;
        if (c->s < c->end && *c->s == '>') c->s++;
        return n;
    }
    c->s++; /* consume '>' */

    if (!parse_children(c, n)) { /* fills until closing tag */
        /* tolerate EOF; return what we have */
        return n;
    }
    /* now at '</tag>' */
    if (c->s + 1 < c->end && c->s[0] == '<' && c->s[1] == '/') {
        c->s += 2;
        while (c->s < c->end && *c->s != '>') c->s++;
        if (c->s < c->end) c->s++;
    }
    return n;
}

XmlNode *xml_parse_memory(const char *data, size_t len) {
    Cur c = {data, data + len};
    XmlNode *root = node_new(dup_range("#root", 5));
    for (;;) {
        while (c.s < c.end && *c.s != '<') c.s++;
        if (c.s >= c.end) break;
        if (skip_misc(&c)) continue;
        XmlNode *el = parse_element(&c);
        if (!el) break;
        node_add_child(root, el);
    }
    return root;
}

XmlNode *xml_parse_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "xml: cannot open '%s'\n", path); return NULL; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return NULL; }
    char *buf = (char *)malloc((size_t)sz);
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); free(buf); return NULL; }
    fclose(fp);
    XmlNode *root = xml_parse_memory(buf, (size_t)sz);
    free(buf);
    return root;
}

/* Free a node's owned members (tag, attrs, child subtrees) but NOT the node
 * struct itself -- children live inlined inside their parent's array. */
static void free_members(XmlNode *n) {
    for (int i = 0; i < n->nattr; i++) { free(n->attrs[i].name); free(n->attrs[i].value); }
    free(n->attrs);
    for (int i = 0; i < n->nchild; i++) free_members(&n->children[i]);
    free(n->children);
    free(n->tag);
}

void xml_free(XmlNode *root) {
    if (!root) return;
    free_members(root);
    free(root); /* only the root is a standalone allocation */
}

const char *xml_attr(const XmlNode *n, const char *name) {
    for (int i = 0; i < n->nattr; i++)
        if (strcmp(n->attrs[i].name, name) == 0) return n->attrs[i].value;
    return NULL;
}

const XmlNode *xml_child(const XmlNode *n, const char *tag) {
    for (int i = 0; i < n->nchild; i++)
        if (strcmp(n->children[i].tag, tag) == 0) return &n->children[i];
    return NULL;
}

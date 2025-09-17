# Remaining Refactoring TODO List
*Priority-ordered tasks for completing TinyUSDZ modularization*

## 🔴 Critical Priority (Blocking Issues)
*None - all critical refactoring complete*

## 🟠 High Priority (Large Files >2000 lines)

### 1. ascii-parser-basetype.cc (3,583 lines)
**Current Issues:**
- Still too large despite being a module
- Mixed parsing responsibilities
- Complex nested logic

**Proposed Modules:**
```
ascii-parser-basetype.cc → Split into:
├── ascii-literal-parser.cc (~600 lines)
├── ascii-container-parser.cc (~800 lines)
├── ascii-type-parser.cc (~700 lines)
├── ascii-array-parser.cc (~600 lines)
├── ascii-dict-parser.cc (~500 lines)
└── ascii-basetype-core.cc (~400 lines)
```

**Tasks:**
- [ ] Extract literal value parsing (bool, int, float, string)
- [ ] Extract container parsing (arrays, tuples)
- [ ] Extract dictionary parsing
- [ ] Extract type name parsing and validation
- [ ] Create unified basetype interface

**Estimated Effort:** 3 days

---

### 2. tydra/scene-access.cc (3,109 lines)
**Current Issues:**
- Multiple responsibilities mixed
- Complex traversal logic
- Query and modification in same file

**Proposed Modules:**
```
scene-access.cc → Split into:
├── scene-traversal.cc (~600 lines)
├── scene-query.cc (~700 lines)
├── scene-modification.cc (~500 lines)
├── scene-validation.cc (~400 lines)
├── scene-statistics.cc (~300 lines)
└── scene-access-core.cc (~600 lines)
```

**Tasks:**
- [ ] Extract traversal utilities and visitors
- [ ] Extract query and search functions
- [ ] Extract modification operations
- [ ] Extract validation logic
- [ ] Extract statistics gathering
- [ ] Create facade interface

**Estimated Effort:** 2 days

---

### 3. c-tinyusd.cc (2,188 lines)
**Current Issues:**
- C API functions not organized
- Mixed concerns
- Difficult to navigate

**Proposed Modules:**
```
c-tinyusd.cc → Split into:
├── c-api-stage.cc (~400 lines)
├── c-api-layer.cc (~350 lines)
├── c-api-prim.cc (~450 lines)
├── c-api-property.cc (~400 lines)
├── c-api-value.cc (~300 lines)
└── c-api-core.cc (~300 lines)
```

**Tasks:**
- [ ] Group stage-related C APIs
- [ ] Group layer-related C APIs
- [ ] Group prim-related C APIs
- [ ] Group property/attribute C APIs
- [ ] Group value type C APIs
- [ ] Create consistent error handling

**Estimated Effort:** 2 days

---

### 4. composition.cc (2,065 lines)
**Current Issues:**
- All composition arcs in one file
- Complex resolution logic
- Difficult to test individually

**Proposed Modules:**
```
composition.cc → Split into:
├── composition-references.cc (~400 lines)
├── composition-payloads.cc (~350 lines)
├── composition-inherits.cc (~300 lines)
├── composition-variants.cc (~400 lines)
├── composition-specializes.cc (~300 lines)
└── composition-resolver.cc (~315 lines)
```

**Tasks:**
- [ ] Extract reference resolution
- [ ] Extract payload handling
- [ ] Extract inheritance logic
- [ ] Extract variant composition
- [ ] Extract specializes arc
- [ ] Create unified resolver

**Estimated Effort:** 2 days

## 🟡 Medium Priority (1500-2000 lines)

### 5. usd-to-json.cc (1,942 lines)
**Proposed Modules:**
- json-prim-converter.cc
- json-value-converter.cc
- json-stage-converter.cc
- json-metadata-converter.cc

**Estimated Effort:** 1.5 days

### 6. usda-reader.cc (1,824 lines)
**Proposed Modules:**
- usda-stage-reader.cc
- usda-property-reader.cc
- usda-metadata-reader.cc
- usda-reader-core.cc

**Estimated Effort:** 1.5 days

### 7. tinyusdz.cc (1,771 lines)
**Proposed Modules:**
- tinyusdz-io.cc
- tinyusdz-stage-api.cc
- tinyusdz-validation.cc
- tinyusdz-core.cc

**Estimated Effort:** 1.5 days

### 8. xform.cc (1,729 lines)
**Proposed Modules:**
- xform-operations.cc
- xform-matrix.cc
- xform-decomposition.cc
- xform-animation.cc

**Estimated Effort:** 1 day

## 🟢 Low Priority (Large Headers)

### 9. value-eval-util.hh (1,948 lines)
- Split evaluation strategies
- Separate type-specific evaluators

### 10. tydra/render-data.hh (1,919 lines)
- Separate class definitions
- Extract interfaces

### 11. typed-array.hh (1,498 lines)
- Split template specializations
- Extract array utilities

### 12. usdGeom.hh (1,273 lines)
- One file per geometry type
- Common geometry base

## 📋 Refactoring Checklist

### For Each File:
- [ ] Analyze dependencies
- [ ] Identify logical modules
- [ ] Create module headers
- [ ] Implement modules
- [ ] Update CMakeLists.txt
- [ ] Test compilation
- [ ] Verify functionality
- [ ] Update documentation
- [ ] Commit changes

### Quality Criteria:
- [ ] No module >800 lines (target: 500)
- [ ] Single responsibility per module
- [ ] Clear module interfaces
- [ ] Minimal inter-module dependencies
- [ ] Consistent naming conventions
- [ ] Proper error handling
- [ ] Documentation for each module

## 📊 Estimated Timeline

### Week 1 (High Priority)
- Monday-Tuesday: ascii-parser-basetype.cc
- Wednesday-Thursday: tydra/scene-access.cc
- Friday: c-tinyusd.cc (start)

### Week 2 (High-Medium Priority)
- Monday: c-tinyusd.cc (complete)
- Tuesday-Wednesday: composition.cc
- Thursday: usd-to-json.cc
- Friday: usda-reader.cc

### Week 3 (Medium Priority)
- Monday-Tuesday: tinyusdz.cc
- Wednesday: xform.cc
- Thursday-Friday: Header reorganization

### Total Estimated Effort:
- **High Priority:** 9 days
- **Medium Priority:** 6 days
- **Low Priority:** 5 days
- **Total:** ~20 days (4 weeks at 50% capacity)

## 🎯 Success Metrics

### Target Metrics:
- Average file size: <500 lines
- Maximum file size: <800 lines
- Module cohesion: >0.8
- Module coupling: <0.3
- Cyclomatic complexity: <10 per function
- Test coverage: >80% for new modules

### Current vs Target:
```
Metric              Current    Target    Gap
Avg File Size       1,200      500       -58%
Max File Size       7,563      800       -89%
Files >2000         8          0         -8
Files >1000         25         5         -20
Macro Usage         10%        0%        -10%
```

## 🚀 Getting Started

### Immediate Next Task:
1. Start with `ascii-parser-basetype.cc`
2. Create module design document
3. Implement extraction incrementally
4. Test each module separately
5. Integrate and verify

### Resources Needed:
- Design patterns guide
- Module interface templates
- Testing framework setup
- Performance benchmarks
- Code review checklist

## 📝 Notes

### Lessons from Completed Work:
1. Factory pattern works well for extensible types
2. Keep interfaces minimal and focused
3. Use forward declarations to reduce coupling
4. Test each module in isolation
5. Document module responsibilities clearly

### Risks to Watch:
1. Template instantiation bloat
2. Circular dependency introduction
3. Performance regression
4. API compatibility breaks
5. Build time increases

---
*This TODO list is a living document and should be updated as work progresses*
*Last Updated: September 18, 2025*
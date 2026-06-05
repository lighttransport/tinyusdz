// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// unit-pcp-cache.h - declarations for tinyusdz::pcp::Cache tests.
//
#pragma once

void pcp_lazy_compute_caches_pointer_test(void);
void pcp_layer_parsed_once_test(void);
void pcp_invalidate_drops_dependents_test(void);
void pcp_payload_load_unload_test(void);
void pcp_buildstage_matches_compgraph_test(void);
void pcp_singlethread_vs_multithread_identical_test(void);
void pcp_mt_shared_reference_test(void);

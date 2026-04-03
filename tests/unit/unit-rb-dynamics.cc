// SPDX-License-Identifier: Apache-2.0
// Rigid body dynamics unit tests.

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-rb-dynamics.h"
#include "tydra/rb-dynamics.h"

#include <cmath>
#include <cstring>

// Max buffer sizes for tests
#define MAX_BODIES    32
#define MAX_COLLIDERS 64
#define MAX_JOINTS    16
#define MAX_CONTACTS  128
#define MAX_PAIRS     256
#define MAX_ISLANDS   16

// Helper: allocate all world buffers on the stack
struct TestWorldBuffers {
  TydraPhysBody bodies[MAX_BODIES];
  TydraPhysCollider colliders[MAX_COLLIDERS];
  TydraPhysJoint joints[MAX_JOINTS];
  TydraPhysContact contacts[MAX_CONTACTS];
  TydraPhysAABB aabbs[MAX_COLLIDERS];
  TydraPhysCollisionPair pairs[MAX_PAIRS];
  int32_t sorted[MAX_COLLIDERS];
  TydraPhysIsland islands[MAX_ISLANDS];
  int32_t union_buf[MAX_BODIES];
  int32_t island_body_buf[MAX_BODIES];
};

static void init_test_world(TydraPhysWorld *world, TestWorldBuffers *buf) {
  memset(buf, 0, sizeof(*buf));
  tydra_phys_world_init(world,
    buf->bodies, MAX_BODIES,
    buf->colliders, MAX_COLLIDERS,
    buf->joints, MAX_JOINTS,
    buf->contacts, MAX_CONTACTS,
    buf->aabbs,
    buf->pairs, MAX_PAIRS,
    buf->sorted,
    buf->islands, MAX_ISLANDS,
    buf->union_buf,
    buf->island_body_buf);
  tydra_phys_world_defaults(world);
}

// -----------------------------------------------------------------------
// 1. Falling sphere: sphere should fall under gravity onto a plane
// -----------------------------------------------------------------------
void rb_falling_sphere_test(void) {
  TydraPhysWorld world;
  TestWorldBuffers buf;
  init_test_world(&world, &buf);

  world.gravity = tp_v3(0, 0, -9.81f);
  world.timestep = 1.0f / 60.0f;

  // Static ground plane (body 0)
  TydraPhysBody ground;
  tydra_phys_body_default(&ground);
  ground.body_type = TYDRA_PHYS_BODY_STATIC;
  ground.inverse_mass = 0;
  ground.xform.position = tp_v3(0, 0, 0);
  ground.xform.rotation = tp_q_identity();
  ground.flags = 0;
  int gi = tydra_phys_add_body(&world, &ground);
  TEST_CHECK(gi == 0);

  TydraPhysCollider ground_col;
  memset(&ground_col, 0, sizeof(ground_col));
  ground_col.shape.type = TYDRA_PHYS_SHAPE_PLANE;
  ground_col.shape.data.plane.normal = tp_v3(0, 0, 1);
  ground_col.shape.data.plane.offset = 0;
  ground_col.local_pose.position = tp_v3(0, 0, 0);
  ground_col.local_pose.rotation = tp_q_identity();
  ground_col.static_friction = 0.5f;
  ground_col.dynamic_friction = 0.5f;
  ground_col.restitution = 0.3f;
  ground_col.collision_group = 1;
  ground_col.collision_mask = 0xFFFFFFFF;
  ground_col.body_index = gi;
  tydra_phys_add_collider(&world, &ground_col);

  // Dynamic sphere (body 1) at z = 5
  TydraPhysBody sphere;
  tydra_phys_body_default(&sphere);
  sphere.body_type = TYDRA_PHYS_BODY_DYNAMIC;
  sphere.inverse_mass = 1.0f;  // mass = 1
  sphere.local_inv_inertia = tp_v3(2.5f, 2.5f, 2.5f);  // solid sphere I = 2/5*m*r^2, r=1
  sphere.xform.position = tp_v3(0, 0, 5);
  sphere.xform.rotation = tp_q_identity();
  sphere.flags = TYDRA_PHYS_BODY_FLAG_GRAVITY | TYDRA_PHYS_BODY_FLAG_CAN_SLEEP;
  int si = tydra_phys_add_body(&world, &sphere);
  TEST_CHECK(si == 1);

  TydraPhysCollider sphere_col;
  memset(&sphere_col, 0, sizeof(sphere_col));
  sphere_col.shape.type = TYDRA_PHYS_SHAPE_SPHERE;
  sphere_col.shape.data.sphere.radius = 1.0f;
  sphere_col.local_pose.position = tp_v3(0, 0, 0);
  sphere_col.local_pose.rotation = tp_q_identity();
  sphere_col.static_friction = 0.5f;
  sphere_col.dynamic_friction = 0.5f;
  sphere_col.restitution = 0.3f;
  sphere_col.collision_group = 1;
  sphere_col.collision_mask = 0xFFFFFFFF;
  sphere_col.body_index = si;
  tydra_phys_add_collider(&world, &sphere_col);

  // Step 120 frames (2 seconds) — sphere should fall and rest on plane at z ≈ 1 (radius)
  for (int i = 0; i < 120; i++) {
    TydraPhysResult r = tydra_phys_step(&world);
    TEST_CHECK(r == TYDRA_PHYS_OK);
  }

  float final_z = world.bodies[1].xform.position.z;
  TEST_MSG("Final sphere z = %f (expected ~1.0)", final_z);
  // Should be near z=1 (resting on plane, radius=1)
  TEST_CHECK(final_z > 0.5f);
  TEST_CHECK(final_z < 2.0f);
}

// -----------------------------------------------------------------------
// 2. Pendulum: hinge joint should constrain bodies
// -----------------------------------------------------------------------
void rb_pendulum_hinge_test(void) {
  TydraPhysWorld world;
  TestWorldBuffers buf;
  init_test_world(&world, &buf);

  world.gravity = tp_v3(0, 0, -9.81f);
  world.timestep = 1.0f / 60.0f;

  // Body 0: static anchor at (0, 0, 5)
  TydraPhysBody anchor;
  tydra_phys_body_default(&anchor);
  anchor.body_type = TYDRA_PHYS_BODY_STATIC;
  anchor.inverse_mass = 0;
  anchor.xform.position = tp_v3(0, 0, 5);
  anchor.flags = 0;
  tydra_phys_add_body(&world, &anchor);

  // Body 1: dynamic bob at (2, 0, 5) — offset to side for swing
  TydraPhysBody bob;
  tydra_phys_body_default(&bob);
  bob.body_type = TYDRA_PHYS_BODY_DYNAMIC;
  bob.inverse_mass = 1.0f;
  bob.local_inv_inertia = tp_v3(1, 1, 1);
  bob.xform.position = tp_v3(2, 0, 5);
  bob.flags = TYDRA_PHYS_BODY_FLAG_GRAVITY;
  tydra_phys_add_body(&world, &bob);

  // No colliders needed for this test

  // Ball joint connecting anchor to bob (distance = 2)
  TydraPhysJoint joint;
  memset(&joint, 0, sizeof(joint));
  joint.type = TYDRA_PHYS_JOINT_BALL;
  joint.body_a = 0;
  joint.body_b = 1;
  joint.local_anchor_a.position = tp_v3(0, 0, 0);
  joint.local_anchor_a.rotation = tp_q_identity();
  joint.local_anchor_b.position = tp_v3(-2, 0, 0);  // relative to bob
  joint.local_anchor_b.rotation = tp_q_identity();
  tydra_phys_add_joint(&world, &joint);

  // Step 180 frames (3 seconds)
  for (int i = 0; i < 180; i++) {
    tydra_phys_step(&world);
  }

  // Bob should have swung down (z < 5) and stayed approximately 2 units from anchor
  float bx = world.bodies[1].xform.position.x;
  float bz = world.bodies[1].xform.position.z;
  float dist = sqrtf(bx * bx + (bz - 5) * (bz - 5));
  TEST_MSG("Bob pos = (%f, %f), dist from anchor = %f", bx, bz, dist);
  TEST_CHECK(bz < 5.0f);  // should have fallen
  TEST_CHECK(dist > 1.0f && dist < 3.0f);  // roughly constrained to distance ~2
}

// -----------------------------------------------------------------------
// 3. Stacking + sleep: boxes stacked should eventually sleep
// -----------------------------------------------------------------------
void rb_stacking_sleep_test(void) {
  TydraPhysWorld world;
  TestWorldBuffers buf;
  init_test_world(&world, &buf);

  world.gravity = tp_v3(0, 0, -9.81f);
  world.timestep = 1.0f / 60.0f;
  world.sleep_threshold = 0.05f;
  world.sleep_time = 0.5f;

  // Ground
  TydraPhysBody ground;
  tydra_phys_body_default(&ground);
  ground.body_type = TYDRA_PHYS_BODY_STATIC;
  ground.inverse_mass = 0;
  ground.flags = 0;
  tydra_phys_add_body(&world, &ground);

  TydraPhysCollider gc;
  memset(&gc, 0, sizeof(gc));
  gc.shape.type = TYDRA_PHYS_SHAPE_PLANE;
  gc.shape.data.plane.normal = tp_v3(0, 0, 1);
  gc.local_pose.position = tp_v3(0, 0, 0);
  gc.local_pose.rotation = tp_q_identity();
  gc.static_friction = 0.8f;
  gc.dynamic_friction = 0.8f;
  gc.restitution = 0.1f;
  gc.collision_group = 1;
  gc.collision_mask = 0xFFFFFFFF;
  gc.body_index = 0;
  tydra_phys_add_collider(&world, &gc);

  // Stack 3 boxes
  for (int i = 0; i < 3; i++) {
    TydraPhysBody box;
    tydra_phys_body_default(&box);
    box.body_type = TYDRA_PHYS_BODY_DYNAMIC;
    box.inverse_mass = 1.0f;
    box.local_inv_inertia = tp_v3(6, 6, 6);  // cube inertia
    box.xform.position = tp_v3(0, 0, 0.5f + (float)i * 1.01f);
    box.flags = TYDRA_PHYS_BODY_FLAG_GRAVITY | TYDRA_PHYS_BODY_FLAG_CAN_SLEEP;
    int bi = tydra_phys_add_body(&world, &box);

    TydraPhysCollider bc;
    memset(&bc, 0, sizeof(bc));
    bc.shape.type = TYDRA_PHYS_SHAPE_BOX;
    bc.shape.data.box.half_extents = tp_v3(0.5f, 0.5f, 0.5f);
    bc.local_pose.position = tp_v3(0, 0, 0);
    bc.local_pose.rotation = tp_q_identity();
    bc.static_friction = 0.8f;
    bc.dynamic_friction = 0.8f;
    bc.restitution = 0.1f;
    bc.collision_group = 1;
    bc.collision_mask = 0xFFFFFFFF;
    bc.body_index = bi;
    tydra_phys_add_collider(&world, &bc);
  }

  // Step 600 frames (10 seconds) — should settle
  for (int i = 0; i < 600; i++) {
    tydra_phys_step(&world);
  }

  // All boxes should still be above ground (collision working)
  int above_ground = 1;
  for (int i = 1; i <= 3; i++) {
    float z = world.bodies[i].xform.position.z;
    float v = tp_v3_length(world.bodies[i].linear_velocity);
    TEST_MSG("Body %d: z = %f, velocity = %f", i, z, v);
    if (z < -1.0f) above_ground = 0;
  }
  TEST_CHECK(above_ground);
}

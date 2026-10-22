# SPDX-License-Identifier: Apache-2.0
import base64
import math
import pathlib
import zipfile

import numpy as np
import pytest
import lightusd
from lightusd import physics, tydra

OBJ = b'''o panel
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 -1
f 1/1/1 2/2/1 3/3/1 4/4/1
'''
URDF = '''<robot name="test"><link name="base"/><link name="arm">
<inertial><origin xyz=".1 .2 .3" rpy="0 0 1.5707963267948966"/>
<mass value="2"/><inertia ixx="1" iyy="2" izz="3" ixy=".1" ixz="0" iyz="0"/></inertial>
<collision><geometry><box size="1 2 3"/></geometry></collision></link>
<joint name="hinge" type="revolute"><parent link="base"/><child link="arm"/>
<origin xyz="1 2 3" rpy="0 0 1.5707963267948966"/><axis xyz="0 1 -1"/>
<limit lower="-1" upper="2"/><dynamics damping=".25"/></joint></robot>'''


def mesh(stage):
    return next(p for p in stage if p.type_name == "Mesh")


def test_obj_polygon_normals_uv_and_no_physics():
    stage = lightusd.load_bytes(OBJ, format="obj")
    m = mesh(stage)
    assert list(m["faceVertexCounts"]) == [4]
    np.testing.assert_allclose(np.asarray(m["normals"]), [[0, 0, -1]] * 4)
    np.testing.assert_allclose(np.asarray(m["primvars:st"]), [[0, 0], [1, 0], [1, 1], [0, 1]])
    assert not physics.inspect(stage).scenes
    assert "metersPerUnit" not in stage.export_usda()


@pytest.mark.parametrize("extension", ["obj", "OBJ"])
def test_obj_direct_file(tmp_path, extension):
    path = tmp_path / f"mesh.{extension}"
    path.write_bytes(OBJ)
    assert mesh(lightusd.load(path)).type_name == "Mesh"
    assert mesh(lightusd.import_obj(path)).type_name == "Mesh"
    assert not lightusd.is_usd(path)


def test_obj_groups_and_negative_indices():
    stage = lightusd.load_bytes(OBJ + b'o second\nf -4/-4/1 -2/-2/1 -1/-1/1\n', format="obj")
    assert len(stage.prims_of_type("Mesh")) == 2


def test_obj_incomplete_uv_warning():
    stage = lightusd.load_bytes(OBJ.replace(b'4/4/1', b'4//1'), format="obj")
    assert "primvars:st" not in mesh(stage)
    assert "incomplete UV" in stage.warnings()


@pytest.mark.parametrize("bad", [b'f 99 98 97\n', b'v nan 0 0\nf 1 1 1\n', b'v 0 0 0\nf 0 0 0\n'])
def test_obj_invalid_rejected(bad):
    with pytest.raises(lightusd.UsdError):
        lightusd.load_bytes(bad, format="obj")


def test_obj_smoothing_and_concave_robot(tmp_path):
    path = tmp_path / "mesh.obj"
    path.write_text('v 0 0 0\nv 2 0 0\nv 2 2 0\nv 1 1 0\nv 0 2 0\ns 1\nf 1 2 3 4 5\n')
    robot = tmp_path / "robot.urdf"
    robot.write_text('<robot name="r"><link name="b"><visual><geometry><mesh filename="mesh.obj"/></geometry></visual></link></robot>')
    m = mesh(lightusd.import_urdf(robot))
    assert list(m["faceVertexCounts"]) == [3, 3, 3]
    points = np.asarray(m["points"])
    indices = np.asarray(m["faceVertexIndices"]).reshape(-1, 3)
    area = sum(np.linalg.norm(np.cross(points[t[1]]-points[t[0]], points[t[2]]-points[t[0]]))/2 for t in indices)
    assert area == pytest.approx(3)


def test_mtl_maps_subsets_and_usdz(tmp_path):
    (tmp_path / "mats").mkdir()
    png = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j1ZkAAAAASUVORK5CYII=')
    (tmp_path / "mats" / "color.png").write_bytes(png)
    (tmp_path / "mats" / "test.mtl").write_text('newmtl red\nKd .8 .1 .2\nKe .2 .3 .4\nNi 1.4\nPr .3\nPm .2\nmap_Kd -s 2 3 1 -o .1 .2 0 -clamp on color.png\nnorm color.png\n')
    path = tmp_path / "mesh.obj"
    path.write_bytes(b'mtllib mats/test.mtl\nusemtl red\n' + OBJ)
    stage = lightusd.load(path)
    assert len(stage.prims_of_type("Material")) == 1
    subset = stage.prims_of_type("GeomSubset")[0]
    assert subset.relationship("material:binding").targets == ("/World/Materials/red",)
    surface = stage.prim_at("/World/Materials/red/PreviewSurface")
    assert surface["inputs:ior"] == pytest.approx(1.4)
    assert surface.attribute("inputs:diffuseColor").connections
    assert surface.attribute("inputs:normal").type_name == "normal3f"
    assert surface.attribute("inputs:normal").connections
    texture = next(p for p in stage if p.type_name == "Shader" and p.get("info:id") == "UsdUVTexture")
    assert texture["inputs:file"] == str(tmp_path / "mats" / "color.png")
    assert texture["inputs:wrapS"] == "clamp"
    for save in (stage.save, stage.save_usdz):
        output = tmp_path / "out.usdz"
        save(str(output))
        with zipfile.ZipFile(output) as archive:
            assert any(archive.read(n) == png for n in archive.namelist() if n.endswith('.png'))
        loaded = lightusd.load(output)
        assert '.png' in loaded.export_usda()
    assert texture["inputs:file"] == str(tmp_path / "mats" / "color.png")


def test_missing_mtl_warns(tmp_path):
    path = tmp_path / "mesh.obj"
    path.write_bytes(b'mtllib absent.mtl\n' + OBJ)
    assert "MTL" in lightusd.load(path).warnings()


def test_urdf_frames_inertia_and_authoring(tmp_path):
    path = tmp_path / "robot.urdf"
    path.write_text(URDF)
    stage = lightusd.load(path)
    data = physics.inspect(stage)
    assert len(data.scenes) == 1
    assert len(data.colliders) == 1
    arm = stage.prim_at("/World/Links/arm")
    np.testing.assert_allclose(np.array(arm.local_transform())[3, :3], [1, 2, 3])
    inertia = np.asarray(arm["physics:diagonalInertia"])
    assert sorted(inertia) == pytest.approx(np.linalg.eigvalsh([[1, .1, 0], [.1, 2, 0], [0, 0, 3]]))
    joint = next(j for j in data.joints if j.prim_path.endswith('/hinge'))
    assert joint.attributes["physics:upperLimit"] == pytest.approx(math.degrees(2))
    assert joint.relationships["physics:body1"] == ('/World/Links/arm',)
    assert joint.attributes["physics:localRot1"] != (1, 0, 0, 0)
    actuator = physics.define_prim(stage, '/World/Drive', 'NewtonActuator', api_schemas=['NewtonPDControlAPI'],
        attributes={'newton:kp': 10}, relationships={'newton:targets': ['/World/Joints/hinge']})
    assert actuator['newton:kp'] == 10
    physics.apply_schema(actuator, 'NewtonMaxEffortClampingAPI', attributes={'newton:maxEffort': 5})
    assert 'NewtonPDControlAPI' in actuator.metadata('apiSchemas')
    assert len(physics.inspect(stage).actuators) == 1
    snapshot = physics.inspect(stage)
    stage.close()
    assert snapshot.actuators[0].attributes['newton:kp'] == 10


def test_urdf_packages_and_missing(tmp_path):
    assets = tmp_path / 'assets'
    assets.mkdir()
    (assets / 'mesh.obj').write_bytes(OBJ)
    path = tmp_path / 'robot.urdf'
    path.write_text('<robot name="r"><link name="b"><visual><geometry><mesh filename="package://part/mesh.obj"/></geometry></visual></link></robot>')
    assert mesh(lightusd.import_urdf(path, packages={'part': assets})).type_name == 'Mesh'
    with pytest.raises(lightusd.UsdError): lightusd.import_urdf(path)
    stage = lightusd.import_urdf(path, allow_missing=True)
    assert not stage.prims_of_type('Mesh')
    assert 'package' in stage.warnings()


def test_mjcf_root_joint_gravity_and_roundtrip(tmp_path):
    text = b'''<mujoco model="r"><option gravity="0 0 -3" timestep=".005"/><worldbody>
    <body name="base" pos="1 2 3"><joint name="hinge" axis="0 1 -1" pos=".1 .2 .3"/>
    <geom type="box" size=".1 .2 .3" group="3"/><body name="tip" pos="0 0 1"><freejoint/>
    <geom type="sphere" size=".1"/></body></body></worldbody>
    <actuator><position name="drive" joint="hinge" kp="20" kv="2" forcerange="-3 3"/></actuator></mujoco>'''
    stage = lightusd.load_bytes(text, format='mjcf')
    assert stage.prim_at('/World/Joints/hinge').type_name == 'PhysicsRevoluteJoint'
    assert stage.prim_at('/World/PhysicsScene')['physics:gravityMagnitude'] == 3
    assert stage.prim_at('/World/PhysicsScene')['newton:timeStepsPerSecond'] == 200
    assert stage.prim_at('/World/Links/tip')['mjc:freeJoint'] is True
    assert stage.prim_at('/World/Actuators/drive')['newton:kp'] == 20
    assert tydra.to_render_scene(stage).physics['joints']
    for ext in ('usda', 'usdc', 'usdz'):
        path = tmp_path / ('out.' + ext)
        stage.save(str(path))
        assert physics.inspect(lightusd.load(path)).actuators[0].attributes['newton:kp'] == 20


def test_xml_detection_includes_and_anchor(tmp_path):
    (tmp_path / 'sub').mkdir()
    (tmp_path / 'sub' / 'mesh.obj').write_bytes(OBJ)
    (tmp_path / 'sub' / 'child.xml').write_text('<mujoco><asset><mesh name="m" file="mesh.obj"/></asset><worldbody><body name="b"><geom type="mesh" mesh="m"/></body></worldbody></mujoco>')
    path = tmp_path / 'main.xml'
    path.write_text('<mujoco><include file="sub/child.xml"/></mujoco>')
    assert mesh(lightusd.load(path)).type_name == 'Mesh'
    (tmp_path / 'sub' / 'child.xml').write_text('<mujoco><include file="../main.xml"/></mujoco>')
    with pytest.raises(lightusd.UsdError, match='[Rr]ecurs'): lightusd.load(path)


@pytest.mark.parametrize('text', [b'<robot>', b'<unknown/>', b'<!DOCTYPE robot><robot/>',
    b'<robot><link name="b"/><joint name="j" type="fixed"><parent link="b"/><child link="b"/></joint></robot>'])
def test_xml_invalid(text):
    with pytest.raises(lightusd.UsdError): lightusd.load_bytes(text, format='urdf')


def test_import_budget_and_resolver(tmp_path):
    with pytest.raises(lightusd.UsdError): lightusd.load_bytes(OBJ, format='obj', max_memory=16)
    resolver = lightusd.AssetResolver()
    resolver.register_memory('virtual.obj', OBJ)
    stage = lightusd.import_obj('virtual.obj', resolver=resolver)
    assert mesh(stage).type_name == 'Mesh'
    with pytest.raises(ValueError): lightusd.load_bytes(OBJ, format='obj', up_axis='X')
    path = tmp_path / 'robot.xml'
    path.write_text('<robot name="r"><link name="b"/></robot>')
    assert physics.inspect(lightusd.load(path)).rigid_bodies


def test_obj_smoothing_groups():
    text = b'v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\ns 1\nf 1 2 3\nf 1 3 4\n'
    smooth = mesh(lightusd.load_bytes(text, format='obj'))
    np.testing.assert_allclose(np.asarray(smooth['normals'])[0], [2**-.5, 0, 2**-.5], atol=1e-6)
    flat = mesh(lightusd.load_bytes(text.replace(b's 1', b's off'), format='obj'))
    assert len(flat['points']) == 6


def test_no_unanchored_asset_reads_and_schema_validation():
    stage = lightusd.load_bytes(b'mtllib unknown.mtl\n' + OBJ, format='obj')
    assert 'source anchor' in stage.warnings()
    prim = stage.prim_at('/World')
    with pytest.raises(ValueError): physics.apply_schema(prim, 'NewtonTypoAPI')
    with pytest.raises(ValueError): physics.apply_schema(prim, 'NewtonSceneAPI', attributes={'newton:unknown': 1})
    physics.apply_schema(prim, 'NewtonSceneAPI', attributes={'newton:unknown': ('double[]', [1, 2])})
    assert tuple(prim['newton:unknown']) == (1, 2)


def test_resolver_callback_exception_propagation():
    resolver = lightusd.AssetResolver()
    def fail(*args):
        raise RuntimeError('import resolver failure')
    resolver.register_scheme('testimport', fail)
    with pytest.raises(RuntimeError, match='import resolver failure'):
        lightusd.import_obj('testimport:mesh.obj', resolver=resolver)


def test_mjcf_motor_bounds_are_not_constant_commands():
    text = b'''<mujoco><worldbody><body name="b"><joint name="j"/>
    <geom type="sphere" size=".1"/></body></worldbody>
    <actuator><motor name="drive" joint="j" gear="2" ctrlrange="-1 1"/></actuator></mujoco>'''
    actuator = lightusd.load_bytes(text, format='mjcf').prim_at('/World/Actuators/drive')
    assert 'newton:constEffort' not in actuator
    assert tuple(actuator['mjc:gear']) == (2,)
    assert tuple(actuator['mjc:ctrlrange']) == (-1, 1)


def test_reserved_world_name_and_filtered_pairs():
    stage = lightusd.load_bytes(b'<robot><link name="__world"/></robot>', format='urdf')
    assert stage.prim_at('/World/Links/__world')['physics:rigidBodyEnabled'] is True
    data = lightusd.loads('''#usda 1.0
    def Xform "a" (prepend apiSchemas = ["PhysicsFilteredPairsAPI"]) {
        rel physics:filteredPairs = </b>
    }
    def Xform "b" {}
    ''')
    assert tydra.to_render_scene(data).physics['filtered_pairs'][0]['targets'] == ('/b',)


def test_visible_mjcf_geom_also_collides():
    stage = lightusd.load_bytes(b'''<mujoco><worldbody><body name="b"><freejoint/>
        <geom name="ball" type="sphere" size=".2" friction=".6 .01 .02" contype="2" conaffinity="4"/>
        </body></worldbody></mujoco>''', format='mjcf')
    data = physics.inspect(stage)
    assert len(data.colliders) == 1
    collider = data.colliders[0]
    assert collider.type_name == 'Sphere'
    assert collider.attributes['mjc:geomFriction'] == pytest.approx((.6, .01, .02))
    assert collider.attributes['mjc:geomContype'] == 2


def test_mjcf_actuator_default_class():
    stage = lightusd.load_bytes(b'''<mujoco><default><default class="drive">
      <general gainprm="4 5 6" biasprm="0 -4 -2" forcerange="-3 3"/>
      </default></default><worldbody><body name="b"><joint name="j"/>
      <geom type="sphere" size=".1"/></body></worldbody>
      <actuator><general name="motor" class="drive" joint="j"/></actuator></mujoco>''', format='mjcf')
    record = physics.inspect(stage).actuators[0]
    assert record.attributes['mjc:gainPrm'] == (4, 5, 6)


def test_mesh_subsets_survive_layer_growth(tmp_path):
    (tmp_path / 'mesh.obj').write_bytes(b'mtllib mats.mtl\n' + OBJ.replace(b'f 1/', b'usemtl red\nf 1/'))
    (tmp_path / 'mats.mtl').write_text('newmtl red\nKd 1 0 0\n')
    geoms = ''.join(f'<visual name="v{i}"><geometry><mesh filename="mesh.obj"/></geometry></visual>' for i in range(90))
    path = tmp_path / 'robot.urdf'
    path.write_text('<robot><link name="base">' + geoms + '</link></robot>')
    stage = lightusd.import_urdf(path)
    assert len(stage.prims_of_type('Mesh')) == 90
    for p in stage.prims_of_type('Mesh'):
        assert sum(p['faceVertexCounts']) == len(p['faceVertexIndices'])


def test_mjcf_compiler_directory_from_include(tmp_path):
    (tmp_path / 'meshes').mkdir()
    (tmp_path / 'meshes' / 'mesh.obj').write_bytes(OBJ)
    (tmp_path / 'robot.xml').write_text('<mujoco><compiler meshdir="meshes"/><worldbody><body name="b"><geom type="sphere" size=".1"/></body></worldbody></mujoco>')
    path = tmp_path / 'scene.xml'
    path.write_text('<mujoco><include file="robot.xml"/><asset><mesh name="m" file="mesh.obj"/></asset><worldbody><geom mesh="m" type="mesh"/></worldbody></mujoco>')
    assert mesh(lightusd.import_mjcf(path)).type_name == 'Mesh'


def test_closed_tendon_route(tmp_path):
    stage = lightusd.load_bytes(b'''<mujoco><worldbody><body name="b">
        <site name="a"/><site name="b" pos="1 0 0"/><geom type="sphere" size=".1"/>
        </body></worldbody><tendon><spatial name="loop"><site site="a"/>
        <site site="b"/><site site="a"/></spatial></tendon></mujoco>''', format='mjcf')
    tendon = stage.prims_of_type('MjcTendon')[0]
    assert len(tendon.relationship('mjc:path').targets) == 2
    assert tuple(tendon['mjc:path:indices']) == (0, 1, 0)
    for ext in ('usda', 'usdc', 'usdz'):
        path = tmp_path / ('loop.' + ext); stage.save(str(path))
        assert tuple(lightusd.load(path).prims_of_type('MjcTendon')[0]['mjc:path:indices']) == (0, 1, 0)


def test_robot_mesh_preserves_skin(tmp_path):
    (tmp_path / 'mesh.obj').write_bytes(OBJ + b'vw 0 0 1\nvw 1 1 1\nvw 2 2 1\nvw 3 3 1\n')
    path = tmp_path / 'robot.urdf'
    path.write_text('<robot><link name="b"><visual><geometry><mesh filename="mesh.obj"/></geometry></visual></link></robot>')
    m = mesh(lightusd.import_urdf(path))
    assert 'SkelBindingAPI' in m.metadata('apiSchemas')
    assert m.attribute('primvars:skel:jointWeights').metadata('elementSize') == 1


def test_worldbody_and_unnamed_sites():
    stage = lightusd.load_bytes(b'''<mujoco><worldbody><site name="fixed" pos="1 2 3"/>
      <body name="b"><site pos="0 0 1"/><geom type="sphere" size=".1"/></body>
      </worldbody></mujoco>''', format='mjcf')
    sites = physics.inspect(stage).sites
    assert len(sites) == 2
    np.testing.assert_allclose(np.asarray(stage.prim_at('/World/Sites/fixed').local_transform())[3, :3], [1, 2, 3])


def test_mjcf_merged_compiler_and_option(tmp_path):
    (tmp_path / 'robot.xml').write_text('<mujoco><compiler angle="radian"/><option timestep=".004"/><worldbody><body name="b" euler="0 0 1.5707963267948966"><geom type="sphere" size=".1"/></body></worldbody></mujoco>')
    path = tmp_path / 'scene.xml'
    path.write_text('<mujoco><compiler meshdir="assets"/><option gravity="0 0 -3"/><include file="robot.xml"/></mujoco>')
    stage = lightusd.import_mjcf(path)
    np.testing.assert_allclose(np.asarray(stage.prim_at('/World/Links/b').local_transform())[:3, :3], [[0,1,0],[-1,0,0],[0,0,1]], atol=1e-6)
    scene = stage.prim_at('/World/PhysicsScene')
    assert scene['physics:gravityMagnitude'] == 3
    assert scene['newton:timeStepsPerSecond'] == 250


@pytest.mark.parametrize('child_angle,parent_angle,lo,hi', [('radian','degree',-.5,.75), ('degree','radian',-30,60)])
def test_attach_joint_angle_units(tmp_path, child_angle, parent_angle, lo, hi):
    (tmp_path / 'child.xml').write_text(f'''<mujoco><compiler angle="{child_angle}"/>
      <default><default class="slider"><joint type="slide" range="-.2 .4"/></default></default>
      <worldbody><body name="root"><body name="tip"><joint name="hinge" range="{lo} {hi}"/>
      <joint name="slide" class="slider"/><geom type="sphere" size=".1"/></body></body></worldbody></mujoco>''')
    path = tmp_path / 'parent.xml'
    path.write_text(f'''<mujoco><compiler angle="{parent_angle}"/>
      <asset><model name="child" file="child.xml"/></asset><worldbody>
      <body name="base"><attach model="child" body="root" prefix="p_"/></body></worldbody></mujoco>''')
    stage = lightusd.import_mjcf(path)
    hinge = stage.prim_at('/World/Joints/p_hinge')
    scale = 180 / math.pi if child_angle == 'radian' else 1
    assert hinge['physics:lowerLimit'] == pytest.approx(lo * scale)
    assert hinge['physics:upperLimit'] == pytest.approx(hi * scale)
    slide = stage.prim_at('/World/Joints/p_slide')
    assert slide['physics:lowerLimit'] == pytest.approx(-.2)
    assert slide['physics:upperLimit'] == pytest.approx(.4)


def test_mjcf_repeated_model_sections():
    stage = lightusd.load_bytes(b'''<mujoco><worldbody><body name="b"><joint name="a"/>
      <joint name="b"/><geom type="sphere" size=".1"/></body></worldbody>
      <tendon><fixed name="t0"><joint joint="a" coef="1"/></fixed></tendon>
      <tendon><fixed name="t1"><joint joint="b" coef="1"/></fixed></tendon>
      <actuator><motor name="a0" joint="a"/></actuator><actuator><motor name="a1" joint="b"/></actuator>
      <equality><joint name="eq0" joint1="a" joint2="b"/></equality>
      <equality><joint name="eq1" joint1="b" joint2="a"/></equality></mujoco>''', format='mjcf')
    data = physics.inspect(stage)
    assert len(data.actuators) == len(data.tendons) == len(data.equalities) == 2


def test_urdf_fixed_axis_rpy():
    roll, pitch, yaw = .4, -.3, .8
    c, s = math.cos, math.sin
    rx = np.array([[1,0,0], [0,c(roll),-s(roll)], [0,s(roll),c(roll)]])
    ry = np.array([[c(pitch),0,s(pitch)], [0,1,0], [-s(pitch),0,c(pitch)]])
    rz = np.array([[c(yaw),-s(yaw),0], [s(yaw),c(yaw),0], [0,0,1]])
    rotation = rz @ ry @ rx
    stage = lightusd.load_bytes(b'''<robot><link name="base"/><link name="arm"/><link name="tip"/>
      <joint name="j" type="fixed"><parent link="base"/><child link="arm"/>
        <origin xyz=".1 .2 .3" rpy=".4 -.3 .8"/></joint>
      <joint name="tip_joint" type="fixed"><parent link="arm"/><child link="tip"/>
        <origin xyz="1 2 3"/></joint></robot>''', format='urdf')
    arm = np.asarray(stage.prim_at('/World/Links/arm').local_transform())
    tip = np.asarray(stage.prim_at('/World/Links/tip').local_transform())
    np.testing.assert_allclose(arm[:3, :3].T, rotation, atol=1e-6)
    np.testing.assert_allclose(tip[3, :3], rotation @ [1,2,3] + [.1,.2,.3], atol=1e-6)

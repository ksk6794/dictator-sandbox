# Prints the bones of the XR mannequins' hands (stage 2a of docs/recon/window3_plan.md): name, parent and the reference pose in
# the mesh's space, for both meshes. Run by the editor's Python commandlet:
#   UnrealEditor-Cmd.exe <project> -run=pythonscript -script="<this file>" -nohmd -DisablePlugins=OpenXR -unattended
import json
import os
import unreal

out = {}
for side in ('left', 'right'):
    path = '/Game/XRMannequins/Meshes/SKM_MannyXR_' + side
    mesh = unreal.load_asset(path)
    if mesh is None:
        out[side] = {'error': 'cannot load ' + path}
        continue
    skeleton = mesh.skeleton
    pose = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    names = unreal.AnimPoseExtensions.get_bone_names(pose)
    bones = []
    for name in names:
        world = unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD)
        local = unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.LOCAL)
        try:
            parent = str(unreal.AnimPoseExtensions.get_parent_bone_name(pose, name))
        except Exception:
            parent = '?'
        t = world.translation
        r = world.rotation
        lt = local.translation
        lr = local.rotation
        bones.append({'name': str(name), 'parent': parent,
                      'cs_t': [t.x, t.y, t.z], 'cs_q': [r.x, r.y, r.z, r.w],
                      'local_t': [lt.x, lt.y, lt.z], 'local_q': [lr.x, lr.y, lr.z, lr.w]})
    bounds = mesh.get_bounds()
    out[side] = {'skeleton': skeleton.get_path_name(), 'bones': bones,
                 'bounds_origin': [bounds.origin.x, bounds.origin.y, bounds.origin.z],
                 'bounds_extent': [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
                 'materials': [str(m.material_interface.get_path_name()) if m.material_interface else '' for m in mesh.materials]}
target = os.path.join(unreal.Paths.project_saved_dir(), 'Recon', 'window3', 'xr_hands.json')
os.makedirs(os.path.dirname(target), exist_ok=True)
with open(target, 'w') as f:
    json.dump(out, f, indent=1)
unreal.log('XR HANDS DUMPED: ' + target)

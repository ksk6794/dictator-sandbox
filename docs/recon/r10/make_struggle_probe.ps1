# R10, step 7's first measure (docs/recon/r10_plan.md: "can the muscles bring a grip to its 450 N for 0.1 s or 45 cm for 0.25 s"):
# a body laid on its back; a hand takes hold of the left forearm at 1.0 s and then holds its place; at 2.0 s the body asks its left hand
# toward its chest and head (a hand_target of weight 1), with whatever strength the run gives its arms. Written to Saved\Recon\r10\struggle.
#   powershell -ExecutionPolicy Bypass -File docs\recon\r10\make_struggle_probe.ps1
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$out = Join-Path $repo 'Saved\Recon\r10\struggle'
New-Item -ItemType Directory -Force $out | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$spec = [ordered]@{
    name = 'struggle_probe_arm'
    duration_s = 6.0
    body = [ordered]@{
        mesh = '/Game/MetaHumans/SandboxNPC/Body/SKM_SandboxNPC_BodyMesh.SKM_SandboxNPC_BodyMesh'
        physics_asset = '/Game/Generated/Physics/PA_SandboxNPC.PA_SandboxNPC'
        control = 'active'
        location_cm = @(0, 90, 0)
        laid_deg = 0
    }
    events = @(
        [ordered]@{ t = 1.0; type = 'effector'; effector = 'hand_right'; at_bone = 'lowerarm_l'; follow_bone_s = 0.6; path = @(
            [ordered]@{ t = 0.0; location_cm = @(40, 90, 60); grip = 0 },
            [ordered]@{ t = 0.4; location_cm = @(0, 0, 0); grip = 0 },
            [ordered]@{ t = 0.5; location_cm = @(0, 0, 0); grip = 1 },
            [ordered]@{ t = 3.0; location_cm = @(0, 0, 0); grip = 1 },
            [ordered]@{ t = 3.1; location_cm = @(0, 0, 0); grip = 0 },
            [ordered]@{ t = 3.6; location_cm = @(0, 0, 60); grip = 0 }) },
        [ordered]@{ t = 2.0; type = 'hand_target'; body = 'hand_l'; offset_cm = @(-20, -40, 25); weight = 1.0 }
    )
    expect = [ordered]@{ max_joint_stretch_cm = 5.0 }
}
[IO.File]::WriteAllText((Join-Path $out 'struggle_probe_arm.json'), ($spec | ConvertTo-Json -Depth 12), $utf8)
Write-Output "written $out\struggle_probe_arm.json"

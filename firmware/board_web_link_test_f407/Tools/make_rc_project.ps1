# Mechanical derivation preserves all tested Keil/toolchain settings.
$ErrorActionPreference = 'Stop'
$firmwareRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$mdkRoot = Join-Path $firmwareRoot 'MDK-ARM'
$source = Join-Path $mdkRoot 'board_web_link_test_f407.uvprojx'
$destination = Join-Path $mdkRoot 'board_rc_web_test_f407.uvprojx'
[xml]$project = Get-Content -LiteralPath $source -Raw -Encoding UTF8
$target = $project.Project.Targets.Target
$target.TargetName = 'board_rc_web_test_f407'
$common = $target.TargetOption.TargetCommonOption
$common.OutputDirectory = 'Objects_RC_Web\'
$common.OutputName = 'board_rc_web_test_f407'
$common.ListingPath = 'Listings_RC_Web\'
$controls = $target.TargetOption.TargetArmAds.Cads.VariousControls
$controls.Define = 'STM32F407xx,WEB_RC_TEST=1,WEB_NO_LCD=1'
$controls.IncludePath += ';..\..\chassis_f407\App'
$group = $target.Groups.Group | Where-Object { $_.GroupName -eq 'Web Link and LCD Test' }
$group.GroupName = 'Web and RC Test - motors disabled'
foreach ($file in @($group.Files.File)) {
    if ($file.FileName -in @('lcd_driver.c', 'lcd_display.c')) {
        $group.Files.RemoveChild($file) | Out-Null
    }
}
$additional = @(
    @('rc_web_test.c', '..\Src\rc_web_test.c'),
    @('rc_capture_core.c', '..\..\chassis_f407\App\rc_capture_core.c'),
    @('safety.c', '..\..\chassis_f407\App\safety.c'),
    @('chassis_math.c', '..\..\chassis_f407\App\chassis_math.c'),
    @('motor_profile.c', '..\..\chassis_f407\App\motor_profile.c')
)
foreach ($entry in $additional) {
    $file = $project.CreateElement('File')
    foreach ($pair in @(@('FileName', $entry[0]), @('FileType', '1'), @('FilePath', $entry[1]))) {
        $node = $project.CreateElement($pair[0])
        $node.InnerText = $pair[1]
        $file.AppendChild($node) | Out-Null
    }
    $group.Files.AppendChild($file) | Out-Null
}
$settings = [Xml.XmlWriterSettings]::new()
$settings.Indent = $true
$settings.Encoding = [Text.UTF8Encoding]::new($false)
$writer = [Xml.XmlWriter]::Create($destination, $settings)
try { $project.Save($writer) } finally { $writer.Dispose() }
$sourceOptions = Join-Path $mdkRoot 'board_web_link_test_f407.uvoptx'
if (Test-Path -LiteralPath $sourceOptions) {
    # Retain this user's working ST-Link/SWD and 1 MB flash-algorithm setup.
    # File-tree UI state is rebuilt from the new project's actual sources.
    [xml]$options = Get-Content -LiteralPath $sourceOptions -Raw -Encoding UTF8
    $options.ProjectOpt.Target.TargetName = 'board_rc_web_test_f407'
    $options.ProjectOpt.Target.TargetOption.OPTLEX.ListingPath = 'Listings_RC_Web\'
    foreach ($node in @($options.ProjectOpt.Group)) {
        if ($node) { $options.ProjectOpt.RemoveChild($node) | Out-Null }
    }
    $optionsPath = Join-Path $mdkRoot 'board_rc_web_test_f407.uvoptx'
    $writer = [Xml.XmlWriter]::Create($optionsPath, $settings)
    try { $options.Save($writer) } finally { $writer.Dispose() }
}
Write-Host "Derived Keil RC target: $destination"

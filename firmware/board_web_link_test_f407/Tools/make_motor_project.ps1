# Derive a separate motor-enabled target; never alter the tested RC target.
param([ValidateSet(30, 60, 100)][int]$OutputPercent = 100)
$ErrorActionPreference = 'Stop'
$mdkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\MDK-ARM'))
$variantSuffix = if ($OutputPercent -eq 100) { '' } else { "_${OutputPercent}pct" }
$targetName = "board_chassis_web_f407$variantSuffix"
$listingPath = "Listings_Motor_Web$variantSuffix\"
[xml]$project = Get-Content -LiteralPath (Join-Path $mdkRoot 'board_rc_web_test_f407.uvprojx') -Encoding UTF8 -Raw
$target = $project.Project.Targets.Target
$target.TargetName = $targetName
$common = $target.TargetOption.TargetCommonOption
$common.OutputDirectory = "Objects_Motor_Web$variantSuffix\"
$common.OutputName = $targetName
$common.ListingPath = $listingPath
$target.TargetOption.TargetArmAds.Cads.VariousControls.Define = 'STM32F407xx,WEB_RC_TEST=1,WEB_NO_LCD=1,WEB_MOTOR_TEST=1'
if ($OutputPercent -ne 100) {
    $target.TargetOption.TargetArmAds.Cads.VariousControls.Define += ",WEB_MOTOR_OUTPUT_PERMILLE=$($OutputPercent * 10)"
}
$group = $target.Groups.Group | Where-Object { $_.GroupName -eq 'Web and RC Test - motors disabled' }
$group.GroupName = 'Chassis RC and Web - MOTOR OUTPUT ENABLED'
$file = $project.CreateElement('File')
foreach ($pair in @(@('FileName','motor_web.c'), @('FileType','1'), @('FilePath','..\Src\motor_web.c'))) {
    $node = $project.CreateElement($pair[0])
    $node.InnerText = $pair[1]
    $file.AppendChild($node) | Out-Null
}
$group.Files.AppendChild($file) | Out-Null
$settings = [Xml.XmlWriterSettings]::new()
$settings.Indent = $true
$settings.Encoding = [Text.UTF8Encoding]::new($false)
$destination = Join-Path $mdkRoot "$targetName.uvprojx"
$writer = [Xml.XmlWriter]::Create($destination, $settings)
try { $project.Save($writer) } finally { $writer.Dispose() }
[xml]$options = Get-Content -LiteralPath (Join-Path $mdkRoot 'board_rc_web_test_f407.uvoptx') -Encoding UTF8 -Raw
$options.ProjectOpt.Target.TargetName = $targetName
$options.ProjectOpt.Target.TargetOption.OPTLEX.ListingPath = $listingPath
foreach ($node in @($options.ProjectOpt.Group)) {
    if ($node) { $options.ProjectOpt.RemoveChild($node) | Out-Null }
}
$writer = [Xml.XmlWriter]::Create((Join-Path $mdkRoot "$targetName.uvoptx"), $settings)
try { $options.Save($writer) } finally { $writer.Dispose() }
Write-Host "Derived MOTOR ENABLED target ($OutputPercent%): $destination"

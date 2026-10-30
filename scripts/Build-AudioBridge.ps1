param(
    [string]$ToolRoot = '',
    [int]$Jobs = 4,
    [switch]$SkipTests,
    [switch]$NoPublish,
    [switch]$Fresh
)
# Retain the old build entry point for existing automation.
& "$PSScriptRoot/Build-ZLBridge.ps1" @PSBoundParameters

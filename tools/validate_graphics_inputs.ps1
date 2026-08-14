[CmdletBinding()]
param()

# Read-only fallback for hosts that do not have a usable Python interpreter.
# The Python validator remains canonical and adds a real restricted-YAML parser,
# image-header validation, and documentation/render-source consistency checks.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$PackageRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$HardwareInputs = Join-Path $PackageRoot 'hardware_reference'
$ManifestRoot = Join-Path $HardwareInputs '04_project_hardware_configuration'
$DiagramRoot = Join-Path $PackageRoot 'hardware_reference\05_project_hardware_diagrams'
$ValidationErrors = [Collections.Generic.List[string]]::new()
$ValidationWarnings = [Collections.Generic.List[string]]::new()
$AllowedStatuses = @(
    'VERIFIED_MEASUREMENT',
    'USER_PHOTO',
    'OFFICIAL_DATASHEET',
    'PROJECT_CONFIGURATION',
    'CANDIDATE_REFERENCE',
    'UNVERIFIED'
)

function Add-ValidationError {
    param([Parameter(Mandatory)][string]$Message)
    $ValidationErrors.Add($Message)
}

function Add-ValidationWarning {
    param([Parameter(Mandatory)][string]$Message)
    $ValidationWarnings.Add($Message)
}

function Get-RelativePackagePath {
    param([Parameter(Mandatory)][string]$Path)
    $full = [IO.Path]::GetFullPath($Path)
    $rootPrefix = $PackageRoot.TrimEnd('\') + '\'
    if ($full.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($rootPrefix.Length).Replace('\', '/')
    }
    return $full
}

function Resolve-PackagePath {
    param(
        [Parameter(Mandatory)][string]$Base,
        [Parameter(Mandatory)][string]$Relative,
        [Parameter(Mandatory)][string]$Description
    )
    $full = [IO.Path]::GetFullPath((Join-Path $Base ($Relative.Replace('/', '\'))))
    $rootPrefix = $PackageRoot.TrimEnd('\') + '\'
    if (-not $full.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Add-ValidationError "$Description escapes the package root: $Relative"
        return $null
    }
    return $full
}

function Assert-RequiredFile {
    param([Parameter(Mandatory)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Add-ValidationError "Missing required file: $(Get-RelativePackagePath $Path)"
    }
}

function Assert-Regex {
    param(
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string]$Pattern,
        [Parameter(Mandatory)][string]$FailureMessage
    )
    if ($Text -notmatch $Pattern) {
        Add-ValidationError $FailureMessage
    }
}

function Get-Sha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Convert-SimpleScalar {
    param([Parameter(Mandatory)][string]$Value)
    $trimmed = $Value.Trim()
    if (($trimmed.StartsWith("'") -and $trimmed.EndsWith("'")) -or
        ($trimmed.StartsWith('"') -and $trimmed.EndsWith('"'))) {
        return $trimmed.Substring(1, $trimmed.Length - 2)
    }
    switch -Regex ($trimmed) {
        '^null$' { return $null }
        '^true$' { return $true }
        '^false$' { return $false }
        '^[-+]?\d+$' { return [long]$trimmed }
        default { return $trimmed }
    }
}

function Read-PhotoRecords {
    param([Parameter(Mandatory)][string]$Path)
    $records = [Collections.Generic.List[Collections.Specialized.OrderedDictionary]]::new()
    $current = $null
    $insideResolution = $false
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^  - id:\s*(.+?)\s*$') {
            if ($null -ne $current) {
                $records.Add($current)
            }
            $current = [ordered]@{ id = (Convert-SimpleScalar $Matches[1]) }
            $insideResolution = $false
            continue
        }
        if ($null -eq $current) {
            continue
        }
        if ($line -match '^    resolution_px:\s*$') {
            $insideResolution = $true
            continue
        }
        if ($insideResolution -and $line -match '^      (width|height):\s*(.*?)\s*$') {
            $current["resolution_$($Matches[1])"] = Convert-SimpleScalar $Matches[2]
            continue
        }
        if ($line -match '^    ([A-Za-z0-9_]+):\s*(.*?)\s*$') {
            $insideResolution = $false
            $current[$Matches[1]] = Convert-SimpleScalar $Matches[2]
        }
    }
    if ($null -ne $current) {
        $records.Add($current)
    }
    return @($records)
}

function Test-ConcreteValue {
    param([AllowNull()]$Value)
    if ($null -eq $Value) {
        return $false
    }
    if ($Value -is [string]) {
        return -not [string]::IsNullOrWhiteSpace($Value)
    }
    return $true
}

function Get-YamlIndentedBlock {
    param(
        [Parameter(Mandatory)][string[]]$Lines,
        [Parameter(Mandatory)][string]$StartPattern,
        [Parameter(Mandatory)][int]$StartIndent
    )
    $start = -1
    for ($index = 0; $index -lt $Lines.Count; $index++) {
        if ($Lines[$index] -match $StartPattern) {
            $start = $index
            break
        }
    }
    if ($start -lt 0) {
        return @()
    }
    $result = [Collections.Generic.List[string]]::new()
    for ($index = $start + 1; $index -lt $Lines.Count; $index++) {
        $line = $Lines[$index]
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            $indent = $line.Length - $line.TrimStart(' ').Length
            if ($indent -le $StartIndent) {
                break
            }
        }
        $result.Add($line)
    }
    return @($result)
}

function Read-BlockScalars {
    param(
        [Parameter(Mandatory)][string[]]$Lines,
        [Parameter(Mandatory)][int]$ScalarIndent
    )
    $record = [ordered]@{}
    $prefix = '^' + (' ' * $ScalarIndent) + '([A-Za-z0-9_]+):\s*(.*?)\s*$'
    foreach ($line in $Lines) {
        if ($line -match $prefix) {
            if (-not [string]::IsNullOrWhiteSpace($Matches[2])) {
                $record[$Matches[1]] = Convert-SimpleScalar $Matches[2]
            }
        }
    }
    return $record
}

function Read-MeasurementGroups {
    param([Parameter(Mandatory)][string]$Path)
    $groups = @{}
    $activeName = $null
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^([A-Za-z0-9_]+):\s*$') {
            $activeName = $Matches[1]
            $groups[$activeName] = [ordered]@{}
            continue
        }
        if ($null -ne $activeName -and $line -match '^  ([A-Za-z0-9_]+):\s*(.*?)\s*$') {
            $groups[$activeName][$Matches[1]] = Convert-SimpleScalar $Matches[2]
        }
    }
    return $groups
}

function Assert-MeasurementRecord {
    param(
        [AllowNull()]$RecordId,
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][bool]$RequireMeasurementRecord
    )
    if (-not (Test-ConcreteValue $RecordId)) {
        Add-ValidationError "$Label requires a nonempty evidence_record_id."
    }
    if ($RequireMeasurementRecord) {
        $metadata = $MeasurementGroups['measurement_metadata']
        if ($null -eq $metadata -or $metadata['status'] -ne 'VERIFIED_MEASUREMENT') {
            Add-ValidationError "$Label requires VERIFIED_MEASUREMENT metadata."
        }
        elseif ($RecordId -ne $metadata['evidence_record_id']) {
            Add-ValidationError "$Label evidence_record_id must match measurement_metadata.evidence_record_id."
        }
    }
}

function Assert-MeasurementGroupFields {
    param(
        [Parameter(Mandatory)][string]$GroupName,
        [Parameter(Mandatory)][string[]]$RequiredFields,
        [Parameter(Mandatory)][string]$Label
    )
    $group = $MeasurementGroups[$GroupName]
    if ($null -eq $group -or $group['status'] -ne 'VERIFIED_MEASUREMENT') {
        Add-ValidationError "$Label requires electrical_measurements.$GroupName.status VERIFIED_MEASUREMENT."
        return
    }
    foreach ($field in $RequiredFields) {
        if (-not (Test-ConcreteValue $group[$field])) {
            Add-ValidationError "$Label lacks concrete measurement field: $field."
        }
    }
}

function Assert-ValueEvidenceTransition {
    param(
        [Parameter(Mandatory)][Collections.Specialized.OrderedDictionary]$Record,
        [Parameter(Mandatory)][string]$ValueKey,
        [Parameter(Mandatory)][string]$StatusKey,
        [AllowNull()]$EvidenceRecordId,
        [Parameter(Mandatory)][string]$Label,
        [string[]]$AllowedVerified = @('VERIFIED_MEASUREMENT')
    )
    $status = $Record[$StatusKey]
    $allowed = @('UNVERIFIED') + $AllowedVerified
    if ($status -notin $allowed) {
        Add-ValidationError "$Label status must be one of $($allowed -join ', '), found '$status'."
        return 'INVALID'
    }
    if ($status -eq 'UNVERIFIED') {
        if ($null -ne $Record[$ValueKey]) {
            Add-ValidationError "$Label is UNVERIFIED but $ValueKey is not null."
        }
    }
    else {
        if (-not (Test-ConcreteValue $Record[$ValueKey])) {
            Add-ValidationError "$Label is $status but $ValueKey is not concrete."
        }
        Assert-MeasurementRecord $EvidenceRecordId $Label ($status -eq 'VERIFIED_MEASUREMENT')
    }
    return $status
}

$requiredFiles = @(
    (Join-Path $ManifestRoot 'project_hardware_profile.yaml'),
    (Join-Path $ManifestRoot 'project_wiring.csv'),
    (Join-Path $ManifestRoot 'gpio_usage_rules.csv'),
    (Join-Path $ManifestRoot 'hardware_photos.yaml'),
    (Join-Path $ManifestRoot 'hardware_measurements.yaml'),
    (Join-Path $HardwareInputs '99_source_integrity\official_files_sha256.txt')
)
foreach ($file in $requiredFiles) {
    Assert-RequiredFile $file
}

$hardwarePath = Join-Path $ManifestRoot 'project_hardware_profile.yaml'
$photoPath = Join-Path $ManifestRoot 'hardware_photos.yaml'
$measurementsPath = Join-Path $ManifestRoot 'hardware_measurements.yaml'
$pinsPath = Join-Path $ManifestRoot 'gpio_usage_rules.csv'
$connectionsPath = Join-Path $ManifestRoot 'project_wiring.csv'
$MeasurementGroups = @{}

foreach ($yamlPath in @($hardwarePath, $photoPath, $measurementsPath)) {
    if (-not (Test-Path -LiteralPath $yamlPath -PathType Leaf)) {
        continue
    }
    $yamlText = Get-Content -LiteralPath $yamlPath -Raw
    foreach ($match in [regex]::Matches($yamlText, '(?m)^\s*(?:status|[A-Za-z0-9_]+_status):\s*([^\s#]+)')) {
        $status = $match.Groups[1].Value.Trim("'", '"')
        if ($AllowedStatuses -notcontains $status) {
            Add-ValidationError "Invalid evidence status in $(Get-RelativePackagePath $yamlPath): $status"
        }
    }
}

if (Test-Path -LiteralPath $hardwarePath -PathType Leaf) {
    $hardwareText = Get-Content -LiteralPath $hardwarePath -Raw
    $hardwareAssertions = [ordered]@{
        '(?m)^schema_version:\s*4\s*$' = 'Hardware manifest schema_version must be 4.'
        '(?m)^  module_profile:\s*N16R8\s*$' = 'Hardware target must be N16R8.'
        '(?m)^  board_identity_evidence_record_id:\s*.*$' = 'Board identity evidence-record field is required.'
        '(?m)^  flash_mb:\s*16\s*$' = 'N16R8 flash profile must be 16 MB.'
        '(?m)^  psram_mb:\s*8\s*$' = 'N16R8 PSRAM profile must be 8 MB.'
        '(?m)^  nonexistent_soc_gpios:\s*\[22, 23, 24, 25\]\s*$' = 'Hardware manifest must list nonexistent ESP32-S3 GPIO22-GPIO25.'
        '(?m)^  internal_memory_path_gpios:\s*\[26, 27, 28, 29, 30, 31, 32, 33, 34\]\s*$' = 'Hardware manifest must list the WROOM GPIO26-GPIO34 memory path.'
        '(?m)^  unavailable_module_gpios:\s*\[35, 36, 37\]\s*$' = 'Hardware manifest must list GPIO35/36/37 as unavailable for N16R8.'
        '(?m)^      d_minus_gpio:\s*19\s*$' = 'Native USB D- must map to GPIO19.'
        '(?m)^      d_plus_gpio:\s*20\s*$' = 'Native USB D+ must map to GPIO20.'
        '(?m)^      exact_board_trace_path:\s*.*$' = 'Native USB exact-board trace-path result field is required.'
        '(?m)^      evidence_record_id:\s*.*$' = 'At least one nested physical evidence-record field is required.'
        '(?m)^    sda_gpio:\s*8\s*$' = 'I2C SDA must use project-configured GPIO8.'
        '(?m)^    scl_gpio:\s*9\s*$' = 'I2C SCL must use project-configured GPIO9.'
        '(?m)^      reference_board_gpio:\s*48\s*$' = 'Official YD-ESP32-S3 reference board RGB LED must remain GPIO48.'
        '(?m)^      reference_board_gpio_status:\s*CANDIDATE_REFERENCE\s*$' = 'YD board RGB mapping must remain evidence-qualified until the physical PCB is matched.'
        '(?m)^  phase_state:\s*DISARMED\s*$' = 'The phase state must remain DISARMED.'
        '(?m)^  arming_supported:\s*false\s*$' = 'Arming must remain unsupported.'
        '(?m)^  physical_waveform_outputs_allowed:\s*false\s*$' = 'Physical waveform output must remain prohibited.'
        '(?m)^  controller:\s*NullActuatorController\s*$' = 'NullActuatorController must be selected.'
        '(?m)^  physical_outputs_enabled:\s*false\s*$' = 'Physical outputs must remain disabled.'
        '(?m)^  output_peripherals_initialized:\s*false\s*$' = 'Output peripherals must remain uninitialized.'
        '(?m)^  usb_evidence_record_id:\s*.*$' = 'Consolidated USB/power evidence-record field is required.'
    }
    foreach ($entry in $hardwareAssertions.GetEnumerator()) {
        Assert-Regex $hardwareText $entry.Key $entry.Value
    }
}

if (Test-Path -LiteralPath $measurementsPath -PathType Leaf) {
    $measurementText = Get-Content -LiteralPath $measurementsPath -Raw
    Assert-Regex $measurementText '(?m)^schema_version:\s*3\s*$' `
        'Electrical measurement manifest schema_version must be 3.'
    Assert-Regex $measurementText '(?m)^  evidence_record_id:\s*.*$' `
        'Electrical measurement metadata evidence_record_id field is required.'
    $MeasurementGroups = Read-MeasurementGroups $measurementsPath
    $measurementNames = @('measurement_metadata', 'sht30', 'gy63_ms5611', 'shared_i2c_bus', 'power_path')
    foreach ($groupName in $measurementNames) {
        if (-not $MeasurementGroups.ContainsKey($groupName)) {
            Add-ValidationError "electrical_measurements.$groupName is missing."
            continue
        }
        $group = $MeasurementGroups[$groupName]
        $status = $group['status']
        if ($status -notin @('UNVERIFIED', 'VERIFIED_MEASUREMENT')) {
            Add-ValidationError "electrical_measurements.$groupName.status must be UNVERIFIED or VERIFIED_MEASUREMENT."
            continue
        }
        $concreteKeys = @(
            $group.Keys | Where-Object { $_ -ne 'status' -and (Test-ConcreteValue $group[$_]) }
        )
        if ($status -eq 'UNVERIFIED' -and $concreteKeys.Count -gt 0) {
            Add-ValidationError (
                "electrical_measurements.$groupName is UNVERIFIED but contains non-null result(s): " +
                ($concreteKeys -join ', ')
            )
        }
        if ($status -eq 'VERIFIED_MEASUREMENT') {
            if ($groupName -eq 'measurement_metadata') {
                foreach ($requiredKey in @(
                    'evidence_record_id', 'measured_at_utc', 'operator',
                    'instrument_make_model', 'instrument_calibration_state'
                )) {
                    if (-not (Test-ConcreteValue $group[$requiredKey])) {
                        Add-ValidationError "Verified measurement metadata lacks concrete field: $requiredKey."
                    }
                }
            }
            elseif ($concreteKeys.Count -eq 0) {
                Add-ValidationError "electrical_measurements.$groupName is VERIFIED_MEASUREMENT but has no results."
            }
        }
    }
    $metadata = $MeasurementGroups['measurement_metadata']
    if ($null -ne $metadata -and -not $metadata.Contains('evidence_record_id')) {
        Add-ValidationError 'electrical_measurements.measurement_metadata.evidence_record_id is required.'
    }
    foreach ($groupName in @('sht30', 'gy63_ms5611', 'shared_i2c_bus', 'power_path')) {
        if ($MeasurementGroups.ContainsKey($groupName) -and
            $MeasurementGroups[$groupName]['status'] -eq 'VERIFIED_MEASUREMENT') {
            if ($metadata['status'] -ne 'VERIFIED_MEASUREMENT' -or
                -not (Test-ConcreteValue $metadata['evidence_record_id'])) {
                Add-ValidationError "electrical_measurements.$groupName requires verified metadata with a nonempty evidence_record_id."
            }
        }
    }
}

if ((Test-Path -LiteralPath $hardwarePath -PathType Leaf) -and
    (Test-Path -LiteralPath $measurementsPath -PathType Leaf)) {
    $hardwareLines = @(Get-Content -LiteralPath $hardwarePath)

    $board = Read-BlockScalars `
        (Get-YamlIndentedBlock $hardwareLines '^board:\s*$' 0) 2
    if (-not $board.Contains('board_identity_evidence_record_id')) {
        Add-ValidationError 'hardware_manifest.board.board_identity_evidence_record_id is required.'
    }
    $boardRecordId = $board['board_identity_evidence_record_id']
    foreach ($entry in @(
        @('exact_manufacturer', 'exact_manufacturer_status', 'Exact board manufacturer'),
        @('pcb_revision', 'pcb_revision_status', 'Exact PCB revision'),
        @('exact_module_marking', 'exact_module_marking_status', 'Exact module marking')
    )) {
        $null = Assert-ValueEvidenceTransition `
            $board $entry[0] $entry[1] $boardRecordId $entry[2] `
            @('USER_PHOTO', 'VERIFIED_MEASUREMENT')
    }

    $nativeUsb = Read-BlockScalars `
        (Get-YamlIndentedBlock $hardwareLines '^    native_usb_otg:\s*$' 4) 6
    foreach ($requiredKey in @('exact_board_trace_path', 'evidence_record_id')) {
        if (-not $nativeUsb.Contains($requiredKey)) {
            Add-ValidationError "hardware_manifest native_usb_otg.$requiredKey is required."
        }
    }
    $usbRecordId = $nativeUsb['evidence_record_id']
    $connectorStatus = Assert-ValueEvidenceTransition `
        $nativeUsb 'connector_identity' 'connector_identity_status' $usbRecordId `
        'Native USB connector identity' @('USER_PHOTO', 'VERIFIED_MEASUREMENT')
    $routeStatus = Assert-ValueEvidenceTransition `
        $nativeUsb 'exact_board_trace_path' 'exact_board_trace_path_status' $usbRecordId `
        'Native USB exact-board trace path'
    if ($connectorStatus -eq 'VERIFIED_MEASUREMENT' -or $routeStatus -eq 'VERIFIED_MEASUREMENT') {
        Assert-MeasurementGroupFields 'power_path' @(
            'native_usb_connector_identity',
            'usb_d_minus_continuity_to_gpio19_verified',
            'usb_d_plus_continuity_to_gpio20_verified'
        ) 'Native USB route verification'
        $powerMeasurements = $MeasurementGroups['power_path']
        foreach ($field in @(
            'usb_d_minus_continuity_to_gpio19_verified',
            'usb_d_plus_continuity_to_gpio20_verified'
        )) {
            if ($powerMeasurements[$field] -ne $true) {
                Add-ValidationError "Native USB route verification requires ${field}: true."
            }
        }
    }

    $i2c = Read-BlockScalars `
        (Get-YamlIndentedBlock $hardwareLines '^  i2c:\s*$' 2) 4
    if (-not $i2c.Contains('evidence_record_id')) {
        Add-ValidationError 'hardware_manifest board.i2c.evidence_record_id is required.'
    }
    $i2cStatus = Assert-ValueEvidenceTransition `
        $i2c 'target_frequency_electrically_verified' 'target_frequency_electrical_status' `
        $i2c['evidence_record_id'] 'Shared I2C target frequency'
    if ($i2cStatus -eq 'VERIFIED_MEASUREMENT') {
        if ($i2c['target_frequency_electrically_verified'] -ne $true) {
            Add-ValidationError 'Shared I2C target-frequency verification value must be true.'
        }
        Assert-MeasurementGroupFields 'shared_i2c_bus' `
            @('rise_time_ns_at_400khz', 'fall_time_ns_at_400khz') `
            'Shared I2C target frequency'
    }

    $rgb = Read-BlockScalars `
        (Get-YamlIndentedBlock $hardwareLines '^    rgb_led:\s*$' 4) 6
    if (-not $rgb.Contains('evidence_record_id')) {
        Add-ValidationError 'hardware_manifest rgb_led.evidence_record_id is required.'
    }
    $rgbRecordId = $rgb['evidence_record_id']
    $selectedRgbStatus = Assert-ValueEvidenceTransition `
        $rgb 'selected_gpio' 'selected_gpio_status' $rgbRecordId `
        'Selected onboard RGB GPIO' @('USER_PHOTO', 'VERIFIED_MEASUREMENT')
    $rgbApplicability = $rgb['applicability_to_current_board_status']
    if ($rgbApplicability -notin @('UNVERIFIED', 'USER_PHOTO', 'VERIFIED_MEASUREMENT')) {
        Add-ValidationError 'RGB mapping applicability status has an invalid physical-evidence transition.'
    }
    elseif ($rgbApplicability -ne 'UNVERIFIED') {
        Assert-MeasurementRecord $rgbRecordId 'RGB mapping applicability' `
            ($rgbApplicability -eq 'VERIFIED_MEASUREMENT')
        if ($rgb['selected_gpio'] -notin @(38, 48)) {
            Add-ValidationError 'Verified RGB mapping applicability requires selected_gpio 38 or 48.'
        }
    }
    elseif ($selectedRgbStatus -ne 'UNVERIFIED') {
        Add-ValidationError 'A selected RGB GPIO cannot be verified while applicability remains UNVERIFIED.'
    }

    foreach ($sensorDefinition in @(
        @('sht30_ambient', 'sht30', 'exact_installed_sensor_variant', 'exact_installed_sensor_variant_status'),
        @('ms5611_baro', 'gy63_ms5611', 'exact_sensor_marking', 'exact_sensor_marking_status')
    )) {
        $sensorId = $sensorDefinition[0]
        $sensor = Read-BlockScalars `
            (Get-YamlIndentedBlock $hardwareLines ("^  - id:\s*" + [regex]::Escape($sensorId) + '\s*$') 2) 4
        if (-not $sensor.Contains('evidence_record_id')) {
            Add-ValidationError "hardware_manifest sensor $sensorId evidence_record_id is required."
        }
        $sensorRecordId = $sensor['evidence_record_id']
        $identityStatus = Assert-ValueEvidenceTransition `
            $sensor $sensorDefinition[2] $sensorDefinition[3] $sensorRecordId `
            "$sensorId exact identity" @('USER_PHOTO', 'VERIFIED_MEASUREMENT')
        $addressStatus = Assert-ValueEvidenceTransition `
            $sensor 'actual_address_7bit' 'actual_address_status' $sensorRecordId `
            "$sensorId actual I2C address"
        $overallStatus = $sensor['status']
        if ($overallStatus -notin @('UNVERIFIED', 'VERIFIED_MEASUREMENT')) {
            Add-ValidationError "$sensorId overall status must be UNVERIFIED or VERIFIED_MEASUREMENT."
        }
        elseif ($overallStatus -eq 'VERIFIED_MEASUREMENT') {
            Assert-MeasurementRecord $sensorRecordId "$sensorId hardware verification" $true
            if ($identityStatus -notin @('USER_PHOTO', 'VERIFIED_MEASUREMENT')) {
                Add-ValidationError "$sensorId hardware verification requires an evidence-backed exact identity."
            }
            if ($addressStatus -ne 'VERIFIED_MEASUREMENT') {
                Add-ValidationError "$sensorId hardware verification requires a measured I2C address."
            }
            $requiredFields = @('supply_voltage_v', 'actual_i2c_address_7bit')
            if ($sensorId -eq 'ms5611_baro') {
                $requiredFields += 'prom_crc_passed_on_actual_hardware'
            }
            Assert-MeasurementGroupFields $sensorDefinition[1] $requiredFields `
                "$sensorId hardware verification"
        }
    }

    $power = Read-BlockScalars `
        (Get-YamlIndentedBlock $hardwareLines '^power:\s*$' 0) 2
    if (-not $power.Contains('usb_evidence_record_id')) {
        Add-ValidationError 'hardware_manifest power.usb_evidence_record_id is required.'
    }
    $powerRecordId = $power['usb_evidence_record_id']
    $powerRequirements = [ordered]@{
        battery_output_under_load_status = @('measured_battery_voltage_under_load_v')
        exact_board_5v_input_path_status = @(
            'measured_battery_voltage_under_load_v',
            'verified_board_5v_or_vin_input_point',
            'verified_input_polarity',
            'sensor_3v3_rail_measured_v'
        )
        exact_vbus_isolation_implementation_status = @(
            'usb_vbus_continuity_to_board_5v_rail_ohm',
            'usb_vbus_isolation_method',
            'usb_vbus_isolation_verified',
            'common_usb_signal_ground_verified'
        )
        usb_enumeration_with_vbus_isolation_status = @(
            'usb_enumeration_with_vbus_blocked_verified'
        )
        reverse_current_status = @(
            'reverse_current_from_battery_to_phone_ma',
            'reverse_current_from_phone_to_battery_or_board_ma'
        )
    }
    foreach ($entry in $powerRequirements.GetEnumerator()) {
        $status = $power[$entry.Key]
        if ($status -notin @('UNVERIFIED', 'VERIFIED_MEASUREMENT')) {
            Add-ValidationError "power.$($entry.Key) must be UNVERIFIED or VERIFIED_MEASUREMENT."
            continue
        }
        if ($status -eq 'VERIFIED_MEASUREMENT') {
            Assert-MeasurementRecord $powerRecordId "power.$($entry.Key)" $true
            Assert-MeasurementGroupFields 'power_path' $entry.Value "power.$($entry.Key)"
            $powerMeasurements = $MeasurementGroups['power_path']
            foreach ($booleanField in @(
                'usb_vbus_isolation_verified',
                'common_usb_signal_ground_verified',
                'usb_enumeration_with_vbus_blocked_verified'
            )) {
                if ($booleanField -in $entry.Value -and $powerMeasurements[$booleanField] -ne $true) {
                    Add-ValidationError "power.$($entry.Key) requires ${booleanField}: true."
                }
            }
        }
    }
}

if (Test-Path -LiteralPath $pinsPath -PathType Leaf) {
    $pinRows = @(Import-Csv -LiteralPath $pinsPath)
    $pinIds = @($pinRows | ForEach-Object { [int]$_.gpio })
    if (@($pinIds | Sort-Object -Unique).Count -ne $pinIds.Count) {
        Add-ValidationError 'Duplicate GPIO values exist in pin_constraints.csv.'
    }
    $pinByGpio = @{}
    foreach ($row in $pinRows) {
        $gpio = [int]$row.gpio
        $pinByGpio[$gpio] = $row
        if ($AllowedStatuses -notcontains $row.verification_status) {
            Add-ValidationError "GPIO$gpio has invalid verification_status '$($row.verification_status)'."
        }
    }
    $expectedPinClasses = [ordered]@{
        0 = 'SENSITIVE'; 3 = 'SENSITIVE'; 8 = 'ASSIGNED'; 9 = 'ASSIGNED';
        19 = 'RESERVED'; 20 = 'RESERVED'; 22 = 'NONEXISTENT'; 23 = 'NONEXISTENT';
        24 = 'NONEXISTENT'; 25 = 'NONEXISTENT'; 26 = 'FORBIDDEN'; 27 = 'FORBIDDEN';
        28 = 'FORBIDDEN'; 29 = 'FORBIDDEN'; 30 = 'FORBIDDEN'; 31 = 'FORBIDDEN';
        32 = 'FORBIDDEN'; 33 = 'FORBIDDEN'; 34 = 'FORBIDDEN'; 35 = 'FORBIDDEN'; 36 = 'FORBIDDEN';
        37 = 'FORBIDDEN'; 38 = 'AVAILABLE_WITH_REVIEW'; 43 = 'RESERVED';
        44 = 'RESERVED'; 45 = 'SENSITIVE'; 46 = 'SENSITIVE'; 48 = 'RESERVED'
    }
    foreach ($entry in $expectedPinClasses.GetEnumerator()) {
        $gpio = [int]$entry.Key
        if (-not $pinByGpio.ContainsKey($gpio) -or $pinByGpio[$gpio].classification -ne $entry.Value) {
            $actual = if ($pinByGpio.ContainsKey($gpio)) { $pinByGpio[$gpio].classification } else { '<missing>' }
            Add-ValidationError "GPIO$gpio must be $($entry.Value), found $actual."
        }
    }
    foreach ($gpio in 22..37) {
        if ($pinByGpio.ContainsKey($gpio) -and $pinByGpio[$gpio].verification_status -ne 'OFFICIAL_DATASHEET') {
            Add-ValidationError "GPIO$gpio existence/memory restriction must use OFFICIAL_DATASHEET status."
        }
    }
}
else {
    $pinByGpio = @{}
}

if (Test-Path -LiteralPath $connectionsPath -PathType Leaf) {
    $connectionRows = @(Import-Csv -LiteralPath $connectionsPath)
    $connectionIds = @($connectionRows | ForEach-Object { $_.connection_id })
    if (@($connectionIds | Sort-Object -Unique).Count -ne $connectionIds.Count) {
        Add-ValidationError 'Duplicate connection_id values exist in wiring_connections.csv.'
    }
    $requiredSignals = @(
        'SHT30_3V3', 'GY63_3V3', 'COMMON_GND', 'I2C_SDA', 'I2C_SCL',
        'USB_D_MINUS', 'USB_D_PLUS', 'MS5611_CSB_LOW', 'MS5611_PS_HIGH',
        'MS5611_SDO_NC', 'SYSTEM_5V', 'USB_VBUS_BLOCKED', 'USB_SIGNAL_GND'
    )
    foreach ($signal in $requiredSignals) {
        if (-not @($connectionRows | Where-Object signal_name -EQ $signal).Count) {
            Add-ValidationError "Required signal missing from wiring_connections.csv: $signal"
        }
    }
    foreach ($row in $connectionRows) {
        if ($AllowedStatuses -notcontains $row.verification_status) {
            Add-ValidationError "Connection $($row.connection_id) has invalid verification status."
        }
        $joined = ($row.PSObject.Properties.Value -join ' ')
        if ($joined -match '(?i)\b(?:MOTOR|ESC|SERVO|PWM|MCPWM|DSHOT|RMT)\b') {
            Add-ValidationError "Physical actuator/output wiring is forbidden: $($row.connection_id)."
        }
        if ($joined -match '(?i)\bVerified\s+(?:5V|VIN|input|connector|path|isolation)\b') {
            Add-ValidationError "Connection $($row.connection_id) overstates unverified hardware evidence."
        }
        $gpioText = "$($row.source_gpio_or_net) $($row.destination_pin_label)"
        foreach ($match in [regex]::Matches($gpioText, '(?i)\bGPIO(\d+)\b')) {
            $gpio = [int]$match.Groups[1].Value
            if (-not $pinByGpio.ContainsKey($gpio)) {
                Add-ValidationError "Connection $($row.connection_id) references unclassified GPIO$gpio."
                continue
            }
            if (@('FORBIDDEN', 'NONEXISTENT') -contains $pinByGpio[$gpio].classification) {
                Add-ValidationError "Connection $($row.connection_id) uses unavailable GPIO$gpio."
            }
            if (@(0, 3, 38, 43, 44, 45, 46, 48) -contains $gpio) {
                Add-ValidationError "Connection $($row.connection_id) uses reserved/sensitive GPIO$gpio."
            }
            $allowedSignal = switch ($gpio) {
                8 { 'I2C_SDA' }
                9 { 'I2C_SCL' }
                19 { 'USB_D_MINUS' }
                20 { 'USB_D_PLUS' }
                default { $null }
            }
            if ($null -ne $allowedSignal -and $row.signal_name -ne $allowedSignal) {
                Add-ValidationError "Connection $($row.connection_id) uses GPIO$gpio for $($row.signal_name), not $allowedSignal."
            }
        }
        if ($row.source_component -eq 'battery_5v' -and @('sht30_ambient', 'ms5611_baro') -contains $row.destination_component) {
            Add-ValidationError 'The 5 V battery must never connect directly to sensor VCC.'
        }
        if ($row.source_component -eq 'usb_host' -and $row.source_pin_label -eq 'USB VBUS') {
            if (@('main_esp32', 'sht30_ambient', 'ms5611_baro', 'battery_5v') -contains $row.destination_component) {
                Add-ValidationError 'Android VBUS must not connect to the board, sensors, or battery rail for power.'
            }
            if (@('power', 'bidirectional') -contains $row.direction) {
                Add-ValidationError 'Android VBUS must be represented as a blocked power path.'
            }
        }
    }

    $semanticChecks = @(
        @('I2C SDA', { $_.signal_name -eq 'I2C_SDA' -and $_.source_gpio_or_net -eq 'GPIO8' }),
        @('I2C SCL', { $_.signal_name -eq 'I2C_SCL' -and $_.source_gpio_or_net -eq 'GPIO9' }),
        @('USB D-', { $_.signal_name -eq 'USB_D_MINUS' -and $_.destination_pin_label -eq 'GPIO19' }),
        @('USB D+', { $_.signal_name -eq 'USB_D_PLUS' -and $_.destination_pin_label -eq 'GPIO20' }),
        @('SHT 3V3', { $_.signal_name -eq 'SHT30_3V3' -and $_.destination_pin_label -eq 'VCC' -and $_.voltage_domain -eq '3.3V' }),
        @('GY-63 3V3', { $_.signal_name -eq 'GY63_3V3' -and $_.destination_pin_label -eq 'VCC' -and $_.voltage_domain -eq '3.3V' }),
        @('MS5611 PS high', { $_.signal_name -eq 'MS5611_PS_HIGH' -and $_.source_gpio_or_net -eq '3V3' -and $_.destination_pin_label -eq 'PS' }),
        @('MS5611 CSB low', { $_.signal_name -eq 'MS5611_CSB_LOW' -and $_.source_gpio_or_net -eq 'GND' -and $_.destination_pin_label -eq 'CSB' }),
        @('MS5611 SDO NC', { $_.signal_name -eq 'MS5611_SDO_NC' -and $_.source_gpio_or_net -eq 'NC' -and $_.direction -eq 'not_connected' }),
        @('battery 5V input', { $_.signal_name -eq 'SYSTEM_5V' -and $_.source_component -eq 'battery_5v' -and $_.destination_pin_label -match 'TO_BE_VERIFIED' -and $_.notes -match 'UNVERIFIED' }),
        @('USB VBUS isolation boundary', { $_.signal_name -eq 'USB_VBUS_BLOCKED' -and $_.destination_component -eq 'required_vbus_isolation_boundary' -and $_.direction -eq 'blocked_power_path' -and $_.notes -match 'UNVERIFIED' }),
        @('USB signal ground', { $_.signal_name -eq 'USB_SIGNAL_GND' -and $_.destination_pin_label -eq 'GND' })
    )
    foreach ($check in $semanticChecks) {
        if (-not @($connectionRows | Where-Object $check[1]).Count) {
            Add-ValidationError "Missing or invalid wiring semantic: $($check[0])."
        }
    }
}

if (Test-Path -LiteralPath $photoPath -PathType Leaf) {
    $photoRecords = @(Read-PhotoRecords $photoPath)
    $requiredPhotoIds = @(
        'current_board_front', 'current_board_back',
        'official_yd_board_front', 'official_yd_hardware_overview',
        'sht30_front', 'sht30_back', 'gy63_front', 'gy63_back'
    )
    $photoIds = @($photoRecords | ForEach-Object { $_['id'] })
    foreach ($photoId in $requiredPhotoIds) {
        if ($photoIds -notcontains $photoId) {
            Add-ValidationError "photo_manifest.yaml is missing $photoId."
        }
    }
    if (@($photoIds | Sort-Object -Unique).Count -ne $photoIds.Count) {
        Add-ValidationError 'Duplicate photo IDs exist in photo_manifest.yaml.'
    }
    $drawingAvailable = $true
    try {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
    }
    catch {
        $drawingAvailable = $false
        Add-ValidationWarning 'System.Drawing is unavailable; fallback validator skipped image-dimension decoding.'
    }
    foreach ($photo in $photoRecords) {
        $photoId = [string]$photo['id']
        if (-not $photo.Contains('path')) {
            Add-ValidationError "Photo $photoId has no path."
            continue
        }
        if (-not $photo.Contains('label_orientation') -or [string]::IsNullOrWhiteSpace([string]$photo['label_orientation'])) {
            Add-ValidationError "Photo $photoId has no label_orientation."
        }
        if (-not $photo.Contains('resolution_width') -or -not $photo.Contains('resolution_height')) {
            Add-ValidationError "Photo $photoId has incomplete resolution metadata."
        }
        $resolved = Resolve-PackagePath $ManifestRoot ([string]$photo['path']) "Photo $photoId path"
        if ($null -eq $resolved) {
            continue
        }
        $present = $photo['present']
        if ($present -eq $false) {
            if (Test-Path -LiteralPath $resolved -PathType Leaf) {
                Add-ValidationError "Photo $photoId is marked absent but exists."
            }
            if ($photo['status'] -ne 'UNVERIFIED' -or $null -ne $photo['sha256']) {
                Add-ValidationError "Missing photo $photoId must be UNVERIFIED with null sha256."
            }
            Add-ValidationWarning "Exact image missing; physical overlay is blocked: $(Get-RelativePackagePath $resolved)"
            continue
        }
        if ($present -ne $true) {
            Add-ValidationError "Photo $photoId present must be true or false."
            continue
        }
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            Add-ValidationError "Photo $photoId is marked present but missing."
            continue
        }
        if (@('USER_PHOTO', 'CANDIDATE_REFERENCE') -notcontains $photo['status']) {
            Add-ValidationError "Present photo $photoId has invalid evidence status."
        }
        if ((Get-Sha256 $resolved) -ne $photo['sha256']) {
            Add-ValidationError "Photo hash mismatch: $(Get-RelativePackagePath $resolved)"
        }
        if ($drawingAvailable) {
            $image = $null
            try {
                $image = [Drawing.Image]::FromFile($resolved)
                if ([long]$photo['resolution_width'] -ne $image.Width -or [long]$photo['resolution_height'] -ne $image.Height) {
                    Add-ValidationError "Photo $photoId resolution metadata does not match the file."
                }
            }
            catch {
                Add-ValidationError "Invalid image $(Get-RelativePackagePath $resolved): $($_.Exception.Message)"
            }
            finally {
                if ($null -ne $image) { $image.Dispose() }
            }
        }
        if ($photo.Contains('source_original')) {
            $original = Resolve-PackagePath $ManifestRoot ([string]$photo['source_original']) "Photo $photoId source_original"
            if ($null -ne $original) {
                if (-not (Test-Path -LiteralPath $original -PathType Leaf)) {
                    Add-ValidationError "Photo source original is missing: $(Get-RelativePackagePath $original)"
                }
                elseif ((Get-Sha256 $original) -ne $photo['source_original_sha256']) {
                    Add-ValidationError "Photo source-original hash mismatch: $(Get-RelativePackagePath $original)"
                }
            }
        }
    }
}

$officialHashPath = Join-Path $HardwareInputs '99_source_integrity\official_files_sha256.txt'
if (Test-Path -LiteralPath $officialHashPath -PathType Leaf) {
    $hashRecordCount = 0
    foreach ($line in Get-Content -LiteralPath $officialHashPath) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -notmatch '^([0-9a-fA-F]{64})\s{2,}(.+)$') {
            Add-ValidationError 'Invalid record in official_files_sha256.txt.'
            continue
        }
        $hashRecordCount++
        $expected = $Matches[1].ToLowerInvariant()
        $target = Resolve-PackagePath $PackageRoot $Matches[2] 'Official datasheet hash path'
        if ($null -eq $target) { continue }
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            Add-ValidationError "Official datasheet is missing: $($Matches[2])"
        }
        elseif ((Get-Sha256 $target) -ne $expected) {
            Add-ValidationError "Official datasheet hash mismatch: $($Matches[2])"
        }
    }
    if ($hashRecordCount -ne 8) {
        Add-ValidationError "Expected 8 official manufacturer-file hash records, found $hashRecordCount."
    }
}

$diagramRequirements = [ordered]@{
    '02_i2c_bus.mmd' = @('GPIO8', 'GPIO9', 'UNVERIFIED')
    '03_usb_ports.mmd' = @('GPIO19', 'GPIO20', 'UNVERIFIED')
    '05_gpio_usage.mmd' = @('GPIO22', 'GPIO25', 'GPIO26', 'GPIO34', 'GPIO35', 'GPIO37', 'NONEXISTENT', 'FORBIDDEN', 'UNVERIFIED')
    '04_power_and_usb_vbus.mmd' = @('VBUS', 'UNVERIFIED')
    '01_system_wiring.mmd' = @('GPIO8', 'GPIO9', 'GPIO19', 'GPIO20', 'UNVERIFIED', 'fail-safe supervised', 'default disabled')
}
foreach ($entry in $diagramRequirements.GetEnumerator()) {
    $path = Join-Path $DiagramRoot $entry.Key
    Assert-RequiredFile $path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    $content = Get-Content -LiteralPath $path -Raw
    foreach ($term in $entry.Value) {
        if (-not $content.Contains($term)) {
            Add-ValidationError "$(Get-RelativePackagePath $path) must visibly include '$term'."
        }
    }
    if ($content -match '(?i)\bverified\s+(?:native|USB|connector|5V|VIN|input|path|isolation)') {
        Add-ValidationError "$(Get-RelativePackagePath $path) overstates unverified physical evidence."
    }
    foreach ($line in ($content -split "`r?`n")) {
        if ($line -match '(?i)\b(?:MOTOR|ESC|SERVO|PWM|MCPWM|DSHOT|RMT)\b' -and
            $line -notmatch '(?i)\b(?:disabled|forbidden|prohibited|not allowed|no physical)\b') {
            Add-ValidationError "$(Get-RelativePackagePath $path) contains an enabled or ambiguous actuator/output feature."
        }
    }
}

foreach ($message in $ValidationWarnings) {
    Write-Output "WARNING: $message"
}
foreach ($message in $ValidationErrors) {
    [Console]::Error.WriteLine("ERROR: $message")
}

if ($ValidationErrors.Count -gt 0) {
    [Console]::Error.WriteLine(
        "PowerShell fallback validation failed with $($ValidationErrors.Count) error(s) and $($ValidationWarnings.Count) warning(s)."
    )
    exit 1
}

Write-Output (
    "PowerShell fallback validation passed with $($ValidationWarnings.Count) warning(s). " +
    'Canonical Python validation was not executed; physical verification remains required.'
)
exit 0

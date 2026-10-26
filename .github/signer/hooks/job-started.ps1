# Pre-job hook for the signing runner (ACTIONS_RUNNER_HOOK_JOB_STARTED).
# Any non-zero exit marks the job failed before any of its steps run, so this fails closed:
# every check must pass, and a missing variable or unreadable policy rejects the job.

$ErrorActionPreference = 'Stop'

function Deny([string] $Reason) {
    Write-Host "::error::Signing runner policy rejected this job: ${Reason}"
    exit 1
}

try {
    $Policy = Get-Content -Path (Join-Path $PSScriptRoot 'policy.json') -Raw | ConvertFrom-Json
} catch {
    Deny "policy file unreadable: $_"
}

$Tag = $env:GITHUB_REF_NAME
$ExpectedWorkflowRef = "$($Policy.repository)/$($Policy.workflowPath)@refs/tags/${Tag}"

$Checks = [ordered]@{
    "server is $($Policy.serverUrl)"           = $env:GITHUB_SERVER_URL -eq $Policy.serverUrl
    "repository is $($Policy.repository)"      = $env:GITHUB_REPOSITORY -eq $Policy.repository
    "event is $($Policy.event)"                = $env:GITHUB_EVENT_NAME -ceq $Policy.event
    'ref is a release tag'                     = ($env:GITHUB_REF_TYPE -ceq 'tag') -and
                                                 ($Tag -cmatch $Policy.tagPattern) -and
                                                 ($env:GITHUB_REF -ceq "refs/tags/${Tag}")
    "workflow is ${ExpectedWorkflowRef}"       = $env:GITHUB_WORKFLOW_REF -ceq $ExpectedWorkflowRef
    "job is $($Policy.job)"                    = $env:GITHUB_JOB -ceq $Policy.job
    'actor is allowlisted'                     = [bool]$env:GITHUB_ACTOR -and
                                                 ($Policy.allowedActors -contains $env:GITHUB_ACTOR)
    'triggering actor is allowlisted'          = [bool]$env:GITHUB_TRIGGERING_ACTOR -and
                                                 ($Policy.allowedActors -contains $env:GITHUB_TRIGGERING_ACTOR)
}

# Job logs are public: report which checks failed, but not the allowlist or actor names.
Write-Host "Signing runner policy check for $env:GITHUB_WORKFLOW_REF job $env:GITHUB_JOB"

$Failed = @()
foreach ( $Check in $Checks.GetEnumerator() ) {
    if ( $Check.Value -eq $true ) {
        Write-Host "  pass: $($Check.Key)"
    } else {
        Write-Host "  FAIL: $($Check.Key)"
        $Failed += $Check.Key
    }
}

if ( $Failed.Count -gt 0 ) {
    Deny ($Failed -join '; ')
}

exit 0

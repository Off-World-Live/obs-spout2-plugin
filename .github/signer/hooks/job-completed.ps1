# Post-job hook for the signing runner (ACTIONS_RUNNER_HOOK_JOB_COMPLETED).
# Removes the checkout, downloaded artifacts and temp files so nothing persists between releases.

foreach ( $Path in @($env:GITHUB_WORKSPACE, $env:RUNNER_TEMP) ) {
    if ( $Path -and ( Test-Path -Path $Path ) ) {
        Get-ChildItem -Path $Path -Force | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    }
}

exit 0

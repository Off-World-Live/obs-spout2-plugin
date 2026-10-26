# Release signing runner

Windows release binaries are signed by the `sign-windows` job in `.github/workflows/push.yaml`,
on a self-hosted runner labelled `signer`. The signing key is a Certum SimplySign cloud
certificate, usable only from a Windows session that is logged in to SimplySign Desktop.

## Who can run code on the signing machine

Every layer below must pass before a job's steps run:

1. **Admin-only tags.** A tag ruleset restricts creating, moving and deleting tags to repository
   admins, so only an admin can start a signing run.
2. **Admin-reviewed workflows.** `.github/` is owned by the `@Off-World-Live/release-maintainers` team in `CODEOWNERS`, and the
   `master` ruleset requires code owner review.
3. **Environment approval.** `sign-windows` deploys to the `release` environment, which requires
   approval from a designated reviewer and only accepts release tags.
4. **Runner-side policy.** `hooks/job-started.ps1` runs before every job and fails it unless the job
   is `sign-windows` from `push.yaml` at a release tag in this repository, pushed and triggered by an
   allowlisted actor. The allowlist is passed to `Install-Signer.ps1` and stored only in the installed
   `policy.json`; the repo copy is empty, which rejects every job. Pull request, dispatch, schedule and branch jobs are always
   rejected. The installed copy lives in `%ProgramData%\gh-signer`, which the runner account cannot
   modify; editing the copy in the repo has no effect until an admin re-runs `Install-Signer.ps1`.
5. **Single-use runner.** The runner is registered just-in-time for one job and is offline the rest
   of the time.
6. **Human key unlock.** Signing only works while a person is logged in to SimplySign Desktop.
7. **Least privilege.** The runner runs as `gh-signer`, a standard user that cannot read the
   maintainer's profile, and actions in the signing job are pinned to commit SHAs.

Fork pull request workflows also require approval for all external contributors.

## One-time setup

The installer runs elevated, so never run it from a checkout that other users can write to. Copy
`.github/signer` from reviewed `master` into an admin-only folder, then, from an elevated PowerShell:

```powershell
takeown /F C:\gh-signer-setup /A /R /D Y
icacls C:\gh-signer-setup /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" "${env:USERNAME}:(OI)(CI)RX"
icacls "C:\gh-signer-setup\*" /reset /T /C
pwsh -NoProfile -ExecutionPolicy Bypass -File C:\gh-signer-setup\Install-Signer.ps1 -AllowedActors <github-login>
```

After the ownership change, only an elevated administrator can modify `C:\gh-signer-setup`.

Create a fine-grained personal access token for releases (see "Release token" below), then
sign in to Windows as `gh-signer` once, start SimplySign Desktop and log in to the Certum account.
Review any "gh-signer can write to" warnings the installer prints.

Re-run the installer after changing anything in `.github/signer/`.

## Release token

`New-SignerJitConfig.ps1` needs a fine-grained personal access token:

- Resource owner: `Off-World-Live`; Repository access: only `obs-spout2-plugin`
- Repository permissions: **Administration: Read and write** (Metadata: Read is added automatically)
- A short expiry; regenerate it when it lapses

Store it encrypted for your Windows user (DPAPI) from your own PowerShell window:

```powershell
New-Item -ItemType Directory -Force "$HOME\.gh-signer" | Out-Null
Read-Host -AsSecureString -Prompt 'Token' | ConvertFrom-SecureString | Set-Content "$HOME\.gh-signer\jit.token"
```

## Releasing

1. Push a release tag (`1.2.3`, `1.2.3-beta1`, `1.2.3-rc1`) as an admin.
2. When the `Sign Windows Binaries` job waits for review, approve the `release` deployment.
3. As your normal (admin) account, register a single-use runner:
   ```powershell
   pwsh -NoProfile -File C:\gh-signer-setup\New-SignerJitConfig.ps1
   ```
4. Switch to the `gh-signer` account, log in to SimplySign Desktop, and run:
   ```powershell
   pwsh -NoProfile -ExecutionPolicy Bypass -File "$env:ProgramData\gh-signer\Start-SigningRunner.ps1"
   ```
   The runner picks up the job, signs the DLLs and installer, uploads the signed artifacts and exits.
5. Log out of SimplySign Desktop. `Create Release` then drafts the GitHub release.

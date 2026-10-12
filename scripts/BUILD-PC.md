# Automatic phone builds on a build PC (optional)

Every push to `main` can build a new APK by itself: a PC of yours runs GitHub's runner, builds the APK with
`scripts\ci-build.ps1` and puts it in a Release of a **private** repository of yours, where your phone picks it up.
GitHub's own machines cannot do it: the build needs your ROM, and the ROM never goes to GitHub.

```
push to main ──> notify-build-pc.yml (GitHub's machine: only a signal)
             ──> your private builds repository: a workflow on the self-hosted runner
             ──> ci-build.ps1 on your PC: ROM -> C -> APK ──> a Release of the private repository
```

Keep the runner and the build workflow in a private repository, never in this one: this repository may be public, and
a self-hosted runner serving a public repository would run anyone's code on your PC. The APKs contain the game, so
their Releases must stay private too.

## Setting it up

1. **The build PC** (Windows, about 15 GB free): GitHub's runner registered to your private builds repository, with a
   label your workflow names. Once, in PowerShell **as administrator**, the build tools:
   `powershell -NoProfile -ExecutionPolicy Bypass -File ph-recomp\scripts\1-INSTALL-TOOLS.ps1`.
   Once, by hand (it shows Android's SDK licence for you to accept): `ANDROID-TOOLS.ps1` with `PH_TOOLS_CACHE` set to
   the tools folder the workflow uses.
2. **Your ROM** in a folder the build PC can read (a NAS share is fine; save its password for the runner's user with
   `cmdkey`). The workflow passes it as `-RomSource` (or `PH_ROM_SOURCE`); it is copied next to the repository once.
3. **The workflow** in the private repository: on `repository_dispatch` (type `ph-push`) and by hand, on the runner:
   check out this repository at `client_payload.sha`, then run
   `scripts\ci-build.ps1 -ReleaseRepo <that repository> -Sha <commit>` with `PH_TOOLS_CACHE`, `PH_KEYSTORE` (your
   signing key, so updates install over the app on your phone) and `GH_TOKEN` (the workflow's own token, to upload).
4. **The signal from here**: in this repository's Settings > Secrets and variables > Actions, the variable
   `BUILD_REPO` (`owner/name` of the private repository) and the secret `BUILD_DISPATCH_TOKEN`: a fine-grained token
   with access to that repository only, permission *Contents: Read and write*.

`ci-build.ps1 -DryRun` checks everything without building. A build keeps the newest 5 Phantom Hourglass Releases
(`-Keep N`) and leaves other Releases of the repository alone. On the phone, install the APK from the Release (the
GitHub app works), or let an app like Obtainium follow the private repository's Releases with a token.

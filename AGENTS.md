# Xbox deployment

- The user's Xbox Series S Device Portal is at `https://10.0.0.168:11443`.
- It uses a self-signed HTTPS certificate. It was reachable without authentication
  when verified on 2026-10-29. Recheck connectivity and device identity before deployment.
- Update the existing Lunalight package in place to retain pairing and settings.
  Do not uninstall the app as part of a routine upgrade.
- Package identity: `50497EliaZammuto.MoonlightUWP`.
- Install the signed x64 `.msixbundle` with its x64 `Microsoft.UI.Xaml.2.7.appx`
  and `Microsoft.VCLibs.x64.14.00.appx` dependencies from the same build artifact.
- Verify deployment status and the installed package version through Device Portal.
- Never put Device Portal passwords or session tokens in this repository. If
  authentication becomes necessary, request credentials through the private secret tool.

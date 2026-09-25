# OpenGenesis-BioCore v0.4.0 — Windows Installation

OpenGenesis-BioCore v0.4.0 is distributed as a portable x64 ZIP. No cloud account or remote service is required.

1. Verify the published SHA-256 for `OpenGenesis-BioCore-0.4.0-windows-x64.zip`.
2. Extract the ZIP to a user-writable directory, for example `C:\OpenGenesis-BioCore`.
3. Open PowerShell in the extracted top-level directory.
4. Confirm the release identity:

```powershell
.\bin\biocore.exe --version
.\bin\biocore-worker.exe --self-test
```

The expected Core version is exactly `0.4.0` and the worker protocol is `2`.

5. Create a project directory and catalog:

```powershell
.\bin\biocore.exe --init-project `
  "$HOME\OpenGenesis-BioCoreProjects\my-project" `
  --name "My OpenGenesis-BioCore Project" `
  --catalog "$HOME\OpenGenesis-BioCoreData\catalog.sqlite"
```

6. Start OpenGenesis-BioCore:

```powershell
.\bin\biocore.exe --serve "$HOME\OpenGenesis-BioCoreProjects\my-project"
```

7. Open the localhost UI printed by the terminal.
8. Paste the bootstrap bearer token printed by the same process into the browser session form.
9. Use the available analysis workflows to select explicit managed inputs and parameters.
10. Use Results and generated exports to inspect local reports and artifacts.

OpenGenesis-BioCore is intended to stay local. Do not expose the listener through port forwarding, reverse proxies,
LAN binds or public tunnels. Native plugins are trusted local executables and are not sandboxed.

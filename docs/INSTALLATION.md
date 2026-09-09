# Install Hammer Addons

This guide gets CS2 Workshop Tools running with the add-on loader on Windows.
You do not need to write commands, install Python or PowerShell, or build anything
when using the compiled portable package.

## Before you start

You need Windows x64, Steam, and CS2 with its Workshop Tools installed.
Open the ordinary Workshop Tools once through Steam to check that they work,
then close both Workshop Tools and CS2.

> **Only install add-ons you trust.** An add-on DLL runs code on your computer,
> much like an EXE. It can contain malware, steal files or account information,
> or damage files accessible to your Windows account. A file sent by a friend
> or posted in a chat is not automatically safe. Check its source and publisher
> before installing it. Hammer Addons does not sandbox or security-review
> third-party add-ons. The tools session's `-insecure` option does **not**
> protect your computer from malicious code.

If you previously installed this framework by replacing a game DLL, follow
[the migration instructions](LAUNCHER.md#migrating-a-replacement-dll-installation)
first. Keep your original backups until that restoration is complete.

## 1. Get the portable package

Get the compiled **hammer-addons-portable.zip** from a project release, when
available. GitHub's **Source code (zip)** and **Code > Download ZIP** contain
source files; they are not the ready-to-run package.

If a release does not include the portable ZIP, someone needs to build the
project first. A successful build produces it in `dist/`; the repository
README has the build instructions.

## 2. Extract everything into its own folder

1. Right-click **hammer-addons-portable.zip** and choose **Extract All**.
2. Choose a permanent location you can write to, such as your **Documents**
   folder.
3. Open the extracted **Hammer Addons** folder.

**Keep this folder outside your CS2 installation.** Do not copy these DLLs over
game files. Do not launch from inside the ZIP or copy only the CMD file elsewhere.

You should have these files together:

```text
Hammer Addons/
  Launch Workshop Tools.cmd    <-- double-click this to start
  tools_launcher.exe
  hammer_addons_runtime.dll
  hammer_addons_ui.dll
  README.md                    <-- this guide
  LAUNCHER.md                  <-- advanced instructions
  addons/
    hello/
      addon.ini
      hello.dll
```

Windows may hide extensions such as `.cmd` and `.exe`; in that case the launcher
appears as **Launch Workshop Tools**.

## 3. Start Workshop Tools

1. Open Steam and sign in.
2. Close any running CS2 or Workshop Tools window.
3. Double-click **Launch Workshop Tools.cmd** in the extracted folder.
4. Valve's normal **Workshop Tools** project-selection window opens, just as it
   does through Steam. Select your project (or create a new one), then click
   **Launch Tools**.
5. Wait for Asset Browser to open. Startup may take about a minute after you
   launch the selected project.

The launcher normally finds CS2 automatically. The project picker opens on every
launch, even if you have only one project. The framework does not automatically
choose a project or reuse an earlier choice. Close the picker to cancel.

**CS2 not found?** Locate your CS2 installation folder and drag that folder onto
**Launch Workshop Tools.cmd**. You can also drag
`game\bin\win64\cs2.exe` onto it. The launcher folder itself stays outside CS2.

## 4. Check that it loaded

The project picker shows **Add-on loader active** and a **Workshop Add-ons**
button in its bottom status bar. Click it
to manage picker add-ons. The bundled hello example runs in the editor, so an
empty picker add-on list is normal until you install a picker-compatible add-on.

In Asset Browser, look for the **Workshop Add-ons** manager. The **Add-ons** tab
should list the bundled **hello** example as loaded. This confirms that the
loader is running; you do not need to read a log file.

In Hammer, open **Help > Workshop Add-ons** to show the manager.
The **Extensions** tab shows registered UI integrations, and **About** describes
the framework.

If something fails, the CMD window stays open with an explanation. See
[troubleshooting](#troubleshooting) below.

## Optional: launch from your desktop

Keep the extracted folder where it is. Create a **shortcut** to
**Launch Workshop Tools.cmd** on your desktop:

1. Right-click an empty area of your desktop and choose **New > Shortcut**.
2. Click **Browse**, select **Launch Workshop Tools.cmd** in the extracted
   folder, and click **Next**.
3. Name it **Workshop Tools with Add-ons** and click **Finish**.

Use that shortcut whenever you want tools with add-ons. If you move or rename
the extracted folder, recreate the shortcut.

## Optional: launch from your Steam library

First complete one successful launch using the CMD file above, then close
Workshop Tools. The Steam shortcut opens the same project picker on each launch.

1. In Steam, open **Games > Add a Non-Steam Game to My Library**.
2. Click **Browse** and open your extracted **Hammer Addons** folder.
3. Select **tools_launcher.exe** and click **Open**.
4. Ensure the launcher is selected, then click **Add Selected Programs**.
5. Find **tools_launcher** in your library. Right-click it, open **Properties**,
   and change its name to **Workshop Tools with Add-ons**.
6. Close Properties and click **Play** on that new entry.

Choose the **EXE** for this shortcut. Leave its launch options empty for the
normal setup. These steps use
[Steam's non-Steam shortcut feature](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC).

You now have a separate library entry for tools with add-ons. CS2's existing
Play menu and launch options stay as they were. Keep the portable folder in
place; moving it breaks the shortcut. If a Steam launch fails and its console
closes too quickly, use the CMD file to read the error.

## Install another add-on

**Read the security warning above before adding a DLL.** Only continue if you
trust the add-on and where it came from.

1. Close Workshop Tools.
2. Extract the add-on's package.
3. Copy its folder, containing `addon.ini` and its DLL, into **Hammer Addons/addons**.
   Keep any other files supplied inside that add-on folder.
4. Start Workshop Tools with the launcher again and check the manager.

For example:

```text
Hammer Addons/
  addons/
    hello/
      addon.ini
      hello.dll
    my_addon/
      addon.ini
      my_addon.dll
```

The manifest must be directly inside the add-on's folder, not inside another
nested copy such as `addons/my_addon/my_addon/addon.ini`.
These native add-ons are separate from Steam Workshop map projects.

To remove an add-on, close Workshop Tools and move that add-on's folder outside
`addons/`. To disable it while keeping its files, open its `addon.ini` in
Notepad, change `enabled=true` to `enabled=false`, and save before restarting.
Tool tags in the manager help you filter packages by intended editor.

## Play regular CS2

Close the tools session, then start **Counter-Strike 2** normally through Steam
and choose the regular game.

The add-on launcher starts tools with `-insecure`, so that session cannot join
VAC-secured servers. It keeps Valve's installed DLLs unchanged. This is not a
guarantee of VAC compatibility; if regular CS2 reports invalid signatures, stop
and restore/verify the game installation before trying again.

## Troubleshooting

| What you see | What to do |
| --- | --- |
| Missing launcher EXE or DLL | Extract the complete portable ZIP again into a separate folder. A source-code ZIP is not the compiled package. |
| CS2 could not be found | Drag your CS2 installation folder or its `cs2.exe` onto the CMD file. |
| CS2 is already running | Close the game and Workshop Tools, then try again. |
| Package must be outside CS2 | Move the complete Hammer Addons folder to Documents, then update your shortcuts. |
| Old installation, original backup or `.local` override detected | Follow the [migration guide](LAUNCHER.md#migrating-a-replacement-dll-installation). Do not delete original backups to silence the error. |
| Unsupported original Asset Browser | A CS2 update may need a newer loader release. Use the supported release; if game files were modified, restore them and verify through Steam. |
| Add-on is failed or missing | Read its status in the manager. Check its folder layout and that it supports this loader version. Advanced details are in `loader.log` beside the launcher. |
| Security software blocks a file | Do not disable protection just to launch it. Check the download source and report the detection to the project or add-on author. |
| A shortcut reports an unknown argument | Remove old `--addon` or `--choose-project` arguments from that shortcut. Project selection now happens in Valve's picker every time. |

For launch arguments, diagnostics and older installations, see the
[advanced launcher guide](LAUNCHER.md).

## Remove the portable installation

Close Workshop Tools. Back up any add-ons or other files you want to keep,
then delete the extracted **Hammer Addons** folder and any shortcuts you created.
Remove the optional non-Steam entry from your library as well.

This removes the portable loader; it does not remove CS2 or your Workshop
projects. Per-user add-on settings may remain. Older replacement-DLL
installations require the migration/uninstall procedure linked above.

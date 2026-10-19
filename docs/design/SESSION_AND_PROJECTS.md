# Session restore, Projects tab, keeping settings on a printer switch

User, 2026-10-19: "I dont want to have to reconfigure and change processes and filaments every time I open and close
the app / applies to printers as well, and per printer configs." "Definitely do not clear or reset settings in a
project unless a user manually clears them. Switching printers counts as clearing them, but maybe we should offer the
options to transfer changed settings to the new printer process?" "Restore the last settings and such by default, but
maybe we should add a projects tab before prepare. It will show saved projects and such, last project that you can
resume. Similar to what exists in anycubic and (i think) orca slicer."

## Today (read from the code)
- Startup (DesktopApp::OnInit): `new_project()` with a hard-coded default printer (`PresetInteractor::
  initialize_config_container_with_default`: "Prusa CORE One", else the first preset), then the first favourite
  printer when "only favourites" is on. Nothing of the last session comes back.
- Close (MainFrame::on_close): Save / Discard / Cancel when the BackupStore counts a project unsaved.
- Unsaved preset changes: a Transfer / Discard / Save dialog exists (PresetSelectionCheck ->
  show_unsaved_changes_dialog); which switches reach it is to be checked in the app.
- Recent projects: AppSettingsAdvanced::recent_projects (the top bar's recent menu). `LeftBarTabs` has a commented
  out `Projects` tab.

## Plan
1. Last settings on startup: on every preset selection / edit (debounced) and on close, store the selected project's
   printer, print, filaments (per slot) and their unsaved edits in the data directory (`session.json`). Startup
   creates the new project from it instead of the hard-coded default. Presets that no longer exist fall back to the
   current default.
2. Per printer memory: the same record kept per printer, so switching back to a printer brings back its last print
   profile, filaments and edits.
3. Printer switch never drops edits silently: when the project has edited settings, the switch shows the existing
   Transfer / Discard / Save dialog (Transfer: the changed settings carried to the new printer's profiles).
4. Projects tab (before Prepare): resume the last project, recent projects (name, thumbnail, path, date), open,
   new. Like Orca's / Anycubic's home page.

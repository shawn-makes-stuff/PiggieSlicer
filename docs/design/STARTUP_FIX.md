# Start-up fix → full printer list — plan

Goal (user's design): the printer picker lists every printer from the vendor templates; only the
printers the user has added are built and evaluated. Then all 70 converted vendors can ship without the
~100 s start-up.

## Where the time goes (read from the code, 2026-10-18)
- `PresetInteractor::load_preset_bundle` has a block marked "TODO: remove this when config wizard is
  ready": for every vendor with no printer configs it creates a printer config from **every** vendor
  template (`HwConfigEvaluator::create_printer_config`), and `update_vendor_presets` then evaluates every
  one of them (`PresetEvaluator::evaluate`, per config, all print / tool / material presets).
- The picker (`AddPrinterPanel::rebuild_printer_list`) builds its cards from those evaluated presets
  (`preset_bundle.evaluated_presets`, system printer presets per config), and "adding" a printer only
  favourites the evaluated preset id (`PrinterAddDialog`: `printer_favorite_presets.insert`). So today
  the picker needs everything evaluated.
- Parsing the vendor YAML is the other part (BundleLoader, with its cache). Not measured yet: measure
  after step 1 with all vendors in place.

## Design
1. **Added printers** — a persisted list of the printer templates the user added (app config, by vendor id
   + template id, e.g. `added_printers`). The loader creates printer configs only for those (and for saved
   user printer configs), so only they are evaluated. Creating a config from a template is cheap; the
   evaluation is not.
2. **Picker from templates** — `AddPrinterPanel` lists `vendor_data.printer_configs` (the templates) with
   the families / order it uses now; a card needs the model name, picture and nozzle / sheet choices,
   which come from the template (`create_printer_config` without evaluating). Adding a printer: add the
   template to `added_printers`, create its config, evaluate just it
   (`PresetInteractor::update_changed_printer_configs({config})` already does one config), favourite the
   resulting system printer preset (as now), select it.
3. **Remove printer** — Orca's "Select/Remove printers": removing takes it out of `added_printers` and
   the favourites (its config is dropped at the next start, or right away if not in use by a project).
4. **First run / migration** — no `added_printers` yet:
   - fresh install: the Welcome picker adds the first printer (as now); until then one default printer
     (the default vendor's first template) is created so a project has a printer.
   - existing setup: evaluate as today once, keep the templates whose system presets are favourites
     (what the printer list shows today), write `added_printers`. One slow start, then fast.
5. **Projects** opening a printer that is not added: create and evaluate that config on demand (the
   project already carries its hw config / runtime presets — check `get_or_create_project_context` and
   the runtime preset path).
6. Then move the parked vendors back (`D:\Projects\PiggieSlicer_V2-orca-import-parked` →
   `resources/presets/orca-import/`), measure start-up, run the import e2e (`tools/orca_import/e2e_test.py`,
   it evaluates every config itself, so it does not depend on the loader).

## Steps (one build per batch)
1. Loader: `added_printers` + create configs only for them (+ saved + first-run default) + migration.
   Shared tests: a bundle with two vendors, only the added printer evaluated; picker data from templates.
2. Picker: cards from templates; add = create + evaluate one config + favourite + select. Remove printer.
3. Projects with a non-added printer (on-demand evaluation).
4. All vendors back; measure; e2e.

## Open questions for the user
- Printer list: Orca shows only added printers (we do); removing one = Orca's "Select/Remove printers"
  in the printer dropdown. Same here?

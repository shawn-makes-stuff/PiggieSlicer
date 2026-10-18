#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigCommon.hpp"
#include "Slic3r/Domain/ConfigDefUtils.hpp"
#include "Slic3r/Domain/OrcaLayout.hpp"

#include "Slic3r/Domain/GCodeFlavor.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "boost/algorithm/string.hpp"
#include "boost/format.hpp"

namespace Slic3r::Domain {

// Implementation of FDM configs is done in this file.

// Define our own marking functions, the regular ones are not accessible in Domain.
static const std::string L(const std::string& s) { return s; }

void fdm_config_init_fn(ConfigDefinitions& defs);

using FDMConfigLocation::Printer;
using FDMConfigLocation::Tool;
using FDMConfigLocation::Print;
using FDMConfigLocation::Filament;
using FDMConfigLocation::Project;
using FDMConfigLocation::Object;
using FDMConfigLocation::Volume;

// Define the static object holding all definitions. Provide list of acceptable
// boxes and the init function.
const ConfigDefinitions& get_defs_fdm() {
    static ConfigDefinitions defs_fdm(
        {Printer, Filament, Print, Tool, Object, Volume, Project}, fdm_config_init_fn
    );
    return defs_fdm;
}

// Now define the init function. This function will be called by ConfigDefinitions
// constructor and will fill the definitions with all the necessary data.
void fdm_config_init_fn(ConfigDefinitions& defs)
{
    using Locations = std::set<ConfigLocation>;
    ConfigItemDef* def = nullptr;

    init_common_fdm_sla_config_items(defs, PrinterTechnology::FFF);

    /* TODO - where does this belong to ?
    def = defs.add("profile_vendor", typeid(std::string));
    def->label = L("Profile vendor");
    def->tooltip = L("Name of profile vendor");
    def->cli = ConfigItemDef::nocli;
    def->init_fn = SET_DEFAULT("");

    def = defs.add("profile_version", typeid(std::string));
    def->label = L("Profile version");
    def->tooltip = L("Version of profile");
    def->cli = ConfigItemDef::nocli;
    def->init_fn = SET_DEFAULT("");

    // temporary workaround for compatibility with older Slicer
    {
        def = defs.add("preset_name", typeid(std::string));
        def->init_fn = SET_DEFAULT("");
    }*/


// Defs from void PrintConfigDef::init_fff_params() follow:
    def = defs.add("arc_fitting", typeid(EnumWrapper));
    def->location = Print;
    def->label = L("Arc fitting");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Enable to get a G-code file which has G2 and G3 moves. "
                     "G-code resolution will be used as the fitting tolerance.");
    def->init_fn = init_with(ArcFittingType::Disabled, {
        { int(ArcFittingType::Disabled), "disabled", L("Disabled") },
        { int(ArcFittingType::EmitCenter), "emit_center", L("Enabled: G2/3 I J") }
    });

    def = defs.add("automatic_extrusion_widths", typeid(bool));
    def->location = Print;
    def->label = L("Automatic extrusion widths calculation");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Automatically calculates extrusion widths based on the nozzle diameter of the currently used extruder. "
                     "This setting is essential for printing with different nozzle diameters.");
    def->init_fn = init_with(false);

    def = defs.add("automatic_infill_combination", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume, Tool };
    def->label = L("Automatic infill combination");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_InfillCombination;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This feature automatically combines infill of several layers and speeds up your print by extruding thicker "
                     "infill layers while preserving thin perimeters, thus maintaining accuracy.");
    def->init_fn = init_with(false);

    def = defs.add("automatic_infill_combination_max_layer_height", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume, Tool };
    def->label = L("Automatic infill combination - Max layer height");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_InfillCombination;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Maximum layer height for combining infill when automatic infill combining is enabled. "
                     "Maximum layer height could be specified either as an absolute in millimeters value or as a percentage of nozzle diameter. "
                     "For printing with different nozzle diameters, it is recommended to use percentage value over absolute value.");
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "nozzle_diameter";
    def->units = {L("mm"), L("%")};;

    // Maximum extruder temperature, bumped to 1500 to support printing of glass.
    const int max_temp = 1500;

    def = defs.add("avoid_crossing_curled_overhangs", typeid(bool));
    def->location = Print;
    def->label = L("Avoid crossing curled overhangs (Experimental)");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelAvoidance;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    // TRN PrintSettings: "Avoid crossing curled overhangs (Experimental)"
    def->tooltip = L("Plan travel moves such that the extruder avoids areas where the filament may be curled up. "
                   "This is mostly happening on steeper rounded overhangs and may cause a crash with the nozzle. "
                   "This feature slows down both the print and the G-code generation.");
    def->init_fn = init_with(false);

    def = defs.add("avoid_crossing_perimeters", typeid(bool));
    def->location = Print;
    def->label = L("Avoid crossing perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelAvoidance;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Optimize travel moves in order to minimize the crossing of perimeters. "
                   "This is mostly useful with Bowden extruders which suffer from oozing. "
                   "This feature slows down both the print and the G-code generation.");
    def->init_fn = init_with(false);

    def = defs.add("avoid_crossing_perimeters_max_detour", typeid(double));
    def->location = Print;
    def->label = L("Avoid crossing perimeters - Max detour length");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelAvoidance;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The maximum detour length for avoid crossing perimeters. "
                     "If the detour is longer than this value, avoid crossing perimeters is not applied for this travel path. ");
    def->units = {L("mm")};
    def->min = 0;
    def->max_literal = 1000;
    def->init_fn = init_with(0.0);

    def = defs.add("bed_temperature", typeid(int));
    def->location = Filament;
    def->label = L("Bed other layers");
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 1;
    def->tooltip = L("Bed temperature for layers after the first one. "
                   "Set this to zero to disable bed temperature control commands in the output.");
    def->units = {L("°C")};
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->init_fn = init_with(0);

    def = defs.add("chamber_temperature", typeid(int));
    def->location = Filament;
    // TRN: Label of a configuration parameter: Nominal chamber temperature.
    def->label = L("Chamber Nominal temperature");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("Chamber temperature");
    def->tooltip = L("Required chamber temperature for the print.\nWhen set to zero, "
                     "the nominal chamber temperature is not set in the G-code.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(0);

    def = defs.add("chamber_minimal_temperature", typeid(int));
    def->location = Filament;
    // TRN: Label of a configuration parameter: Minimal chamber temperature
    def->label = L("Chamber Minimal temperature");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("Chamber minimal temperature");
    def->tooltip = L("Minimal chamber temperature that the printer waits for before the print starts. This allows "
                     "to start the print before the nominal chamber temperature is reached.\nWhen set to zero, "
                     "the minimal chamber temperature is not set in the G-code.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(0);

    def = defs.add("bed_temperature_extruder", typeid(int));
    def->location = Print;
    def->label = L("Bed temperature by extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder which determines bed temperatures. "
                     "Set to 0 to determine temperatures based on the first printing extruder "
                     "of the first and the second layers.");
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("before_layer_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("Before layer change G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This custom code is inserted at every layer change, right before the Z move. "
                   "Note that you can use placeholder variables for all Slic3r settings as well "
                   "as [layer_num] and [layer_z].");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->init_fn = init_with("");

    def = defs.add("between_objects_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("Between objects G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This code is inserted between objects when using sequential printing. By default extruder and bed temperature are reset using non-wait command; however if M104, M109, M140 or M190 are detected in this custom code, Slic3r will not add temperature commands. Note that you can use placeholder variables for all Slic3r settings, so you can put a \"M109 S[first_layer_temperature]\" command wherever you want.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("");

    def = defs.add("bottom_solid_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Bottom solid layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_TopBottomShells;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 3;
    // def->row_group = L("Solid layers"); Temporary removed, row_group are not supported in PrintTool
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Number of solid layers to generate on bottom surfaces.");
    def->min = 0;
    def->init_fn = init_with(3);

    def = defs.add("bottom_solid_min_thickness", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_TopBottomShells;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 4;
    // def->row_group = L("Minimum shell thickness"); Temporary removed, row_group are not supported in PrintTool
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The number of bottom solid layers is increased above bottom_solid_layers if necessary to satisfy "
    				 "minimum thickness of bottom shell.");
    def->label = L("Minimum bottom shell thickness");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("bridge_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool };
    def->label = L("Bridge");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_BridgesAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for bridges. "
                   "Set zero to disable acceleration control for bridges.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("bridge_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Bridging angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Bridging angle override. If left to zero, the bridging angle will be calculated "
                   "automatically. Otherwise the provided angle will be used for all bridges. "
                   "Use 180° for zero angle.");
    def->units = {L("°")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("internal_bridge_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Internal bridge infill direction");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Internal Bridging angle override. If left to zero, the bridging angle will be calculated "
                     "automatically for each specific bridge. Otherwise the provided angle will be used according to: - "
                     "The absolute coordinates - The absolute coordinates + Model rotation: If Align directions to model "
                     "is enabled - The optimal automatic angle + this value: If 'Relative Bridge Angle' is enabled Use "
                     "180° for zero absolute angle.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 180;
    def->init_fn = init_with(0.);

    def = defs.add("relative_bridge_angle", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Relative bridge angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("When enabled, the bridge angle values are added to the automatically calculated bridge direction "
                     "instead of overriding it.");
    def->init_fn = init_with(false);

    def = defs.add("bridge_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("External bridge density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Controls the density (spacing) of external bridge lines. Theoretically, 100% means a solid bridge, "
                     "but due to the tendency of bridge extrusions to sag, 100% may not be sufficient. - Higher than 100% "
                     "density (Recommended Max 125%): - Pros: Produces smoother bridge surfaces, as overlapping lines "
                     "provide additional support during printing. - Cons: Can cause overextrusion, which may reduce lower "
                     "and upper surface quality and increase the risk of warping. - Lower than 100% density (Min 10%): - "
                     "Pros: Can create a string-like first layer. Faster and with better cooling because there is more "
                     "space for air to circulate around the extruded bridge. - Cons: May lead to sagging and poorer "
                     "surface finish.");
    def->units = {L("%")};
    def->min = 10;
    def->max = 125;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("internal_bridge_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Internal bridge density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Controls the density (spacing) of internal bridge lines. Internal bridges act as intermediate "
                     "support between sparse infill and top solid infill and can strongly affect top surface quality. - "
                     "Higher than 100% density (Recommended Max 125%): - Pros: Improves internal bridge strength and "
                     "support under top layers, reducing sagging and improving top-surface finish. - Cons: Increases "
                     "material use and print time; excessive density may cause overextrusion and internal stresses. - "
                     "Lower than 100% density (Min 10%): - Pros: Can reduce pillowing and improve cooling (more airflow "
                     "through the bridge), and may speed up printing. - Cons: May reduce internal support, increasing the "
                     "risk of sagging and top surface defects. This option works particularly well when combined with the "
                     "second internal bridge over infill option to improve bridging further before solid infill is "
                     "extruded.");
    def->units = {L("%")};
    def->min = 10;
    def->max = 125;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("internal_bridge_flow", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Internal bridge flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This value governs the thickness of the internal bridge layer. This is the first layer over sparse "
                     "infill so increasing it may increase strength and upper layer quality. Values above 1.0: Increase "
                     "the amount of material while maintaining line spacing. This can improve line contact and strength. "
                     "Values below 1.0: Reduce the amount of material while adjusting line spacing to maintain contact. "
                     "This can improve sagging. The actual bridge flow used is calculated by multiplying this value with "
                     "the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("enable_extra_bridge_layer", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Extra bridge layers (beta)");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("This option enables the generation of an extra bridge layer over internal and/or external bridges. "
                     "Extra bridge layers help improve bridge appearance and reliability, as the solid infill is better "
                     "supported. This is especially useful in fast printers, where the bridge and solid infill speeds "
                     "vary greatly. The extra bridge layer results in reduced pillowing on top surfaces, as well as "
                     "reduced separation of the external bridge layer from its surrounding perimeters. It is generally "
                     "recommended to set this to at least 'External bridge only', unless specific issues with the sliced "
                     "model are found. Options: 1. Disabled - does not generate second bridge layers. This is the default "
                     "and is set for compatibility purposes 2. External bridge only - generates second bridge layers for "
                     "external-facing bridges only. Please note that small bridges that are shorter or narrower than the "
                     "set number of perimeters will be skipped as they would not benefit from a second bridge layer. If "
                     "generated, the second bridge layer will be extruded parallel to the first bridge layer to reinforce "
                     "the bridge strength 3. Internal bridge only - generates second bridge layers for internal bridges "
                     "over sparse infill only. Please note that the internal bridges count towards the top shell layer "
                     "count of your model. The second internal bridge layer will be extruded as close to perpendicular to "
                     "the first as possible. If multiple regions in the same island, with varying bridge angles are "
                     "present, the last region of that island will be selected as the angle reference 4. Apply to all - "
                     "generates second bridge layers for both internal and external-facing bridges");
    def->init_fn = init_with(
        EnableExtraBridgeLayer::Disabled,
        {{int(EnableExtraBridgeLayer::Disabled), "disabled", L("Disabled")},
         {int(EnableExtraBridgeLayer::ExternalBridgeOnly), "external_bridge_only", L("External bridge only")},
         {int(EnableExtraBridgeLayer::InternalBridgeOnly), "internal_bridge_only", L("Internal bridge only")},
         {int(EnableExtraBridgeLayer::ApplyToAll), "apply_to_all", L("Apply to all")}}
    );

    def = defs.add("dont_filter_internal_bridges", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Filter out small internal bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("This option can help reduce pillowing on top surfaces in heavily slanted or curved models. By "
                     "default, small internal bridges are filtered out and the internal solid infill is printed directly "
                     "over the sparse infill. This works well in most cases, speeding up printing without too much "
                     "compromise on top surface quality. However, in heavily slanted or curved models, especially where "
                     "too low a sparse infill density is used, this may result in curling of the unsupported solid "
                     "infill, causing pillowing. Enabling limited filtering or no filtering will print internal bridge "
                     "layer over slightly unsupported internal solid infill. The options below control the sensitivity of "
                     "the filtering, i.e. they control where internal bridges are created: 1. Filter - enables this "
                     "option. This is the default behavior and works well in most cases 2. Limited filtering - creates "
                     "internal bridges on heavily slanted surfaces while avoiding unnecessary bridges. This works well "
                     "for most difficult models 3. No filtering - creates internal bridges on every potential internal "
                     "overhang. This option is useful for heavily slanted top surface models; however, in most cases, it "
                     "creates too many unnecessary bridges.");
    def->init_fn = init_with(
        InternalBridgeFilter::Disabled,
        {{int(InternalBridgeFilter::Disabled), "disabled", L("Filter")},
         {int(InternalBridgeFilter::Limited), "limited", L("Limited filtering")},
         {int(InternalBridgeFilter::NoFilter), "nofilter", L("No filtering")}}
    );

    def = defs.add("sparse_infill_rotate_template", typeid(std::string));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Sparse infill rotation template");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 12;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Rotate the sparse infill direction per layer using a template of angles. Enter comma-separated "
                     "degrees (e.g., '0,30,60,90'). Angles are applied in order by layer and repeat when the list ends. "
                     "Advanced syntax is supported: '+5' rotates +5° every layer; '+5#5' rotates +5° every 5 layers. See "
                     "the Wiki for details. When a template is set, the standard infill direction setting is ignored. "
                     "Note: some infill patterns (e.g., Gyroid) control rotation themselves; use with care.");
    def->units = {L("°")};
    def->init_fn = init_with(std::string());

    def = defs.add("extra_solid_infills", typeid(std::string));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Insert solid layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 14;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Insert solid infill at specific layers. Use N to insert every Nth layer, N#K to insert K consecutive solid "
                     "layers every N layers (K is optional, e.g. '5#' equals '5#1'), or a comma-separated list (e.g. 1,7,9) to "
                     "insert at explicit layers. Layers are 1-based.");
    def->init_fn = init_with(std::string());

    def = defs.add("solid_infill_rotate_template", typeid(std::string));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Solid infill rotation template");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 13;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This parameter adds a rotation of solid infill direction to each layer according to the specified "
                     "template. The template is a comma-separated list of angles in degrees, e.g. '0,90'. The first angle "
                     "is applied to the first layer, the second angle to the second layer, and so on. If there are more "
                     "layers than angles, the angles will be repeated. Note that not all solid infill patterns support "
                     "rotation.");
    def->units = {L("°")};
    def->init_fn = init_with(std::string());

    def = defs.add("align_infill_direction_to_model", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Align directions to model");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 14;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Aligns infill, bridge, ironing, and top/bottom surface directions to follow the model's orientation "
                     "on the build plate. When enabled, these directions rotate together with the model so the printed "
                     "features keep their intended orientation relative to the part, preserving optimal strength and "
                     "surface characteristics regardless of how the model is placed.");
    def->init_fn = init_with(false);

    def = defs.add("counterbore_hole_bridging", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Bridge counterbore holes");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 11;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("This option creates bridges for counterbore holes, allowing them to be printed without support. "
                     "Available modes include: 1. None: No bridge is created 2. Partially Bridged: Only a part of the "
                     "unsupported area will be bridged 3. Sacrificial Layer: A full sacrificial bridge layer is created");
    def->init_fn = init_with(
        CounterboreHoleBridging::None,
        {{int(CounterboreHoleBridging::None), "none", L("None")},
         {int(CounterboreHoleBridging::PartiallyBridge), "partiallybridge", L("Partially bridged")},
         {int(CounterboreHoleBridging::SacrificialLayer), "sacrificiallayer", L("Sacrificial layer")}}
    );

    def = defs.add("thick_internal_bridges", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Thick internal bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, internal bridge extrusion uses a line height equal to the nozzle diameter. This "
                     "increases internal bridge strength and reliability when printed over sparse infill, but may worsen "
                     "appearance. If disabled, internal bridges may look better but can be less reliable over sparse "
                     "infill.");
    def->init_fn = init_with(true);

    def = defs.add("elefant_foot_layers_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Elephant foot layers density");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 22;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Density of internal solid infill for Elephant foot layers compensation. The initial value for the "
                     "second layer is set. Subsequent layers become linearly denser by the height specified in "
                     "elefant_foot_compensation_layers.");
    def->units = {L("%")};
    def->min = 50;
    def->max = 100;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("bridge_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Bridges fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This fan speed is enforced during all bridges and overhangs.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with( 100 );

    def = defs.add("bridge_flow_ratio", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Bridge flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of plastic for bridging. "
                   "You can decrease it slightly to pull the extrudates and prevent sagging, "
                   "although default settings are usually good and you should experiment "
                   "with cooling (use a fan) before tweaking this.");
    def->min = 0.;
    def->max = 2.;
    def->init_fn = init_with(1.);

    def = defs.add("top_one_perimeter_type", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Single perimeter on top surfaces");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_OnlyOnePerimeter;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Use only one perimeter on flat top surface, to give more space to the top infill pattern. Could be applied on topmost surface or all top surfaces.");
    def->init_fn = init_with(
        TopOnePerimeterType::None,
        {{int(TopOnePerimeterType::None), "none", L("Disabled")},
         {int(TopOnePerimeterType::TopSurfaces), "top", L("All top surfaces")},
         {int(TopOnePerimeterType::TopmostOnly), "topmost", L("Topmost surface only")}}
    );

    def = defs.add("only_one_perimeter_first_layer", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Only one perimeter on first layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_OnlyOnePerimeter;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Use only one perimeter on the first layer.");
    def->init_fn = init_with(false);

    def = defs.add("bridge_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for printing bridges.");
    def->units = {L("mm/s")};
    def->aliases = { "bridge_feed_rate" };
    def->min = 0;
    def->init_fn = init_with(60.);

    def = defs.add("internal_bridge_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Internal");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Speed of internal bridges. If the value is expressed as a percentage, it will be calculated based "
                     "on the bridge_speed. Default value is 150%.");
    def->units = {L("mm/s"), L("%")};
    def->min = 1;
    // a percentage of the (per tool) bridge speed: resolved in GCodeGenerator::_extrude
    def->full_label = L("Internal bridges");
    def->init_fn = init_with(FloatOrPercentage(Percentage{150.}));

    def = defs.add("over_bridge_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    // TRN: Label for speed used to print infill above bridges.
    def->label = L("Over bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Speed for printing solid infill above bridges. Set to 0 to use solid infill speed. "
                    "If set as percentage, the speed is calculated over solid infill speed. ");
    def->units = {L("mm/s"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "solid_infill_speed";

    def             = defs.add("enable_dynamic_overhang_speeds", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("Enable dynamic overhang speeds");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip    = L("This setting enables dynamic speed control on overhangs.");
    def->init_fn = init_with(false);

    def = defs.add("slowdown_for_curled_perimeters", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Slow down for curled perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable this option to slow down printing in areas where perimeters may have curled upwards. For "
                     "example, additional slowdown will be applied when printing overhangs on sharp corners like the "
                     "front of the Benchy hull, reducing curling which compounds over multiple layers. It is generally "
                     "recommended to have this option switched on unless your printer cooling is powerful enough or the "
                     "print speed is slow enough that perimeter curling does not happen. If printing with a high external "
                     "perimeter speed, this parameter may introduce wall artifacts when slowing down, due to the "
                     "potentially large variance in print speeds causing the extruder to be unable to keep up with the "
                     "requested flow change. Root cause of these artifacts is most likely PA tuning being slightly off, "
                     "especially when combined with a high PA smooth time. Recommendations when enabling this option: 1. "
                     "Reduce Pressure Advance smooth time to 0.015 - 0.02 so the extruder reacts quickly to the speed "
                     "changes. 2. Increase the minimum print speeds to limit the magnitude of the slowdown and reduce the "
                     "variance between fast and slow segments. 3. If artifacts still appear, enable Extrusion Rate "
                     "Smoothing (ERS) to further smooth the flow transitions. Note: When this option is enabled, overhang "
                     "perimeters are treated like overhangs, meaning the overhang speed is applied even if the "
                     "overhanging perimeter is part of a bridge. For example, when the perimeters are 100% overhanging, "
                     "with no wall supporting them from underneath, the 75% overhang speed will be applied, limited by "
                     "the bridge speed.");
    def->init_fn = init_with(true);

    // TRN PrintSettings : "Dynamic overhang speed"
    auto overhang_speed_setting_description = L("Overhang size is expressed as a percentage of overlap of the extrusion with the previous layer: "
                        "100% would be full overlap (no overhang), while 0% represents full overhang (floating extrusion, bridge). "
                        "Speeds for overhang sizes in between are calculated via linear interpolation. "
                        "If set as percentage, the speed is calculated over the external perimeter speed. "
                        "Note that the speeds generated to gcode will never exceed the max volumetric speed value.");

    def             = defs.add("overhang_speed_0", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("speed for 0% overlap (bridge)");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type   = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip    = overhang_speed_setting_description;
    def->units      = {L("mm/s"), L("%")};
    def->min        = 0;
    def->init_fn = init_with(FloatOrPercentage{15.});
    def->ratio_over = "external_perimeter_speed";

    def             = defs.add("overhang_speed_1", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("speed for 25% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 2;
    def->gui_type   = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip    = overhang_speed_setting_description;
    def->units      = {L("mm/s"), L("%")};
    def->min        = 0;
    def->init_fn = init_with(FloatOrPercentage{15.});
    def->ratio_over = "external_perimeter_speed";

    def             = defs.add("overhang_speed_2", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("speed for 50% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip    = overhang_speed_setting_description;
    def->units      = {L("mm/s"), L("%")};
    def->min        = 0;
    def->init_fn = init_with(FloatOrPercentage{20.});
    def->ratio_over = "external_perimeter_speed";

    def             = defs.add("overhang_speed_3", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("speed for 75% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 4;
    def->gui_type   = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip    = overhang_speed_setting_description;
    def->units      = {L("mm/s"), L("%")};
    def->min        = 0;
    def->init_fn = init_with(FloatOrPercentage{25.});
    def->ratio_over = "external_perimeter_speed";

    def          = defs.add("enable_dynamic_fan_speeds", typeid(bool));
    def->location = Filament;
    def->label   = L("Enable dynamic fan speeds");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category   = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This setting enables dynamic fan speed control on overhangs.");
    def->init_fn = init_with(false);

    // TRN FilamentSettings : "Dynamic fan speeds"
    auto fan_speed_setting_description = L("Overhang size is expressed as a percentage of overlap of the extrusion with the previous layer: "
        "100% would be full overlap (no overhang), while 0% represents full overhang (floating extrusion, bridge). "
        "Fan speeds for overhang sizes in between are calculated via linear interpolation.");

    def           = defs.add("overhang_fan_speed_0", typeid(int));
    def->location = Filament;
    def->label    = L("speed for 0% overlap (bridge)");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = fan_speed_setting_description;
    def->units = {L("%")};
    def->min      = 0;
    def->max      = 100;
    def->init_fn = init_with(0);

    def           = defs.add("overhang_fan_speed_1", typeid(int));
    def->location = Filament;
    def->label    = L("speed for 25% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = fan_speed_setting_description;
    def->units = {L("%")};
    def->min      = 0;
    def->max      = 100;
    def->init_fn = init_with(0);

    def           = defs.add("overhang_fan_speed_2", typeid(int));
    def->location = Filament;
    def->label    = L("speed for 50% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = fan_speed_setting_description;
    def->units = {L("%")};
    def->min      = 0;
    def->max      = 100;
    def->init_fn = init_with(0);

    def           = defs.add("overhang_fan_speed_3", typeid(int));
    def->location = Filament;
    def->label    = L("speed for 75% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = fan_speed_setting_description;
    def->units = {L("%")};
    def->min      = 0;
    def->max      = 100;
    def->init_fn = init_with(0);

    def = defs.add("brim_width", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Brim width");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The horizontal width of the brim that will be printed around each object on the first layer. "
                     "When raft is used, no brim is generated (use raft_first_layer_expansion).");
    def->units = {L("mm")};
    def->min      = 0;
    def->max      = 200;
    def->init_fn  = init_with(5.);

    def = defs.add("brim_type", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Brim type");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("The places where the brim will be printed around each object on the first layer.");
    def->init_fn = init_with(
        BrimType::NoBrim,
        {{int(BrimType::NoBrim), "no_brim", L("No brim")},
         {int(BrimType::OuterOnly), "outer_only", L("Outer brim only")},
         {int(BrimType::InnerOnly), "inner_only", L("Inner brim only")},
         {int(BrimType::OuterAndInner), "outer_and_inner", L("Outer and inner brim")},
         {int(BrimType::BrimEars), "brim_ears", L("Mouse ear")},
         {int(BrimType::AutoBrim), "auto_brim", L("Auto")}}
    );

    def = defs.add("brim_ears_max_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Brim ear max angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum angle to let a brim ear appear. If set to 0, no brim will be created. If set to ~180, brim "
                     "will be created on everything but straight sections.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 180;
    def->init_fn = init_with(125.);

    def = defs.add("brim_ears_detection_length", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Brim ear detection radius");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 11;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The geometry will be decimated before detecting sharp angles. This parameter indicates the minimum "
                     "length of the deviation for the decimation. 0 to deactivate.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("brim_ears_outer_only", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Brim ears outer only");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 12;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Generate mouse ears only on the outer contour of the model, excluding holes and enclosed sections.");
    def->init_fn = init_with(false);

    def = defs.add("brim_flow_ratio", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Brim flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 13;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for brims. The actual brim flow used is calculated by "
                     "multiplying this value by the filament flow ratio, and if set, the object's flow ratio. Note: The "
                     "resulting value will not be affected by the first-layer flow ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("brim_use_efc_outline", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Brim follows compensated outline");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 14;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("When enabled, the brim is aligned with the first-layer perimeter geometry after Elephant Foot "
                     "Compensation is applied. This option is intended for cases where Elephant Foot Compensation "
                     "significantly alters the first-layer footprint. If your current setup already works well, enabling "
                     "it may be unnecessary and can cause the brim to fuse with upper layers.");
    def->init_fn = init_with(false);

    def = defs.add("combine_brims", typeid(bool));
    def->location = Print;
    def->label = L("Combine brims");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 15;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Combine multiple brims into one when they are close to each other. This can improve brim adhesion.");
    def->init_fn = init_with(false);

    def = defs.add("brim_separation", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Brim separation gap");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Offset of brim from the printed object. The offset is applied after the elephant foot compensation.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    /* TODO: Isn't this one legacy? Doesn't the legacy loader remove it ?
    * It is part of s_project_options...

    def = defs.add("colorprint_heights", typeid(std::vector<double>));
    def->label = L("Colorprint height");
    def->tooltip = L("Heights at which a filament change is to occur.");
    def->init_fn = [](ConfigItem& item) { item.vec<double>() = {}; };*/

    /* TODO: How to handle this crap?
    def = defs.add("compatible_printers", typeid(std::vector<std::string>));
    def->label = L("Compatible printers");
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("compatible_printers_condition", typeid(std::string));
    def->label = L("Compatible printers condition");
    def->tooltip = L("A boolean expression using the configuration values of an active printer profile. "
                   "If this expression evaluates to true, this profile is considered compatible "
                   "with the active printer profile.");
    def->init_fn = SET_DEFAULT( new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("compatible_prints", typeid(std::vector<std::string>));
    def->label = L("Compatible print profiles");
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("compatible_prints_condition", typeid(std::string));
    def->label = L("Compatible print profiles condition");
    def->tooltip = L("A boolean expression using the configuration values of an active print profile. "
                   "If this expression evaluates to true, this profile is considered compatible "
                   "with the active print profile.");
    def->init_fn = SET_DEFAULT( new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "compatible_printers_condition" values over the print and filament profiles.
    def = defs.add("compatible_printers_condition_cummulative", typeid(std::vector<std::string>));
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;
    def = defs.add("compatible_prints_condition_cummulative", typeid(std::vector<std::string>));
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("print_order", typeid(EnumWrapper));
    def->location = Print;
    def->label = L("Intra-layer order");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Order in which object instances are visited within a single layer, which controls how much travel "
                     "is spent moving between them. Default: nearest-neighbor chaining, refined with 2-opt and crossing "
                     "removal. A good general choice. As object list: instances are printed in the same order as the "
                     "object list, without any path optimization. Use it when you need a predictable, manually controlled "
                     "order. Best of all (shortest path): every strategy is evaluated and the shortest one is used. The "
                     "object instance order is decided once for the whole print, while the ordering of individual islands "
                     "is decided per layer, so different layers may end up using different strategies. Slightly slower to "
                     "slice. Snake: serpentine row-by-row traversal, refined with 2-opt. Well suited to regular grids of "
                     "many small parts. With multiple filaments or tools in the same layer, minimizing tool changes takes "
                     "priority: objects are grouped by filament first and this setting only orders the instances within "
                     "each filament group, so the overall sequence may not look like the shortest path across the plate.");
    def->init_fn = init_with(
        PrintOrder::Default,
        {{int(PrintOrder::Default), "default", L("Default")},
         {int(PrintOrder::AsObjectList), "as_obj_list", L("As object list")},
         {int(PrintOrder::BestOfStrategies), "best_of", L("Best of all (shortest path)")},
         {int(PrintOrder::Snake), "snake", L("Snake")}}
    );

    def = defs.add("complete_objects", typeid(bool));
    def->location = Print;
    def->label = L("Complete individual objects");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("When printing multiple objects or copies, this feature will complete "
                   "each object before moving onto next one (and starting it from its bottom layer). "
                   "This feature is useful to avoid the risk of ruined prints. "
                   "Slic3r should warn and prevent you from extruder collisions, but beware.");
    def->init_fn = init_with(false);

    def               = defs.add("cooling", typeid(bool));
    def->location     = Filament;
    def->label        = L("Enable auto cooling");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingLogic;
    def->category     = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type     = ConfigItemDef::GUIType::checkbox;
    def->tooltip =
        L("This flag enables the automatic cooling logic that adjusts print speed "
          "and fan speed according to layer printing time.");
    def->init_fn = init_with(true);

    def               = defs.add("cooling_slowdown_logic", typeid(EnumWrapper));
    def->location     = Filament;
    def->label        = L("Cooling slowdown logic");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingLogic;
    def->category     = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type     = ConfigItemDef::GUIType::combobox;
    def->tooltip      = L(
        "Determines how the printer slows down layer printing when the minimum layer time isn't reached. "
             "'Consistent surface' first tries to preserve the print speeds of the first two perimeters by slowing all other features. "
             "Only if this isn't sufficient, it also slows down those first two perimeters. "
             "'Uniform cooling' slows down all print features, including the first two perimeters."
    );
    def->init_fn = init_with(
        CoolingSlowdownLogicType::UniformCooling,
        {
            {int(CoolingSlowdownLogicType::UniformCooling),
             "uniform_cooling",
             L("Uniform cooling")},
            {int(CoolingSlowdownLogicType::ConsistentSurface),
             "consistent_surface",
             L("Consistent surface")},
        }
    );

    def               = defs.add("cooling_perimeter_transition_distance", typeid(double));
    def->location     = Filament;
    def->label        = L("Perimeter transition distance");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingLogic;
    def->category     = ConfigItemDef::Category::Filament_Cooling;
    def->order = 2;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip      = L(
        "Distance in millimeters before non-slowed perimeters where the original unslowed print speed is restored. "
             "This reduces print quality issues when transitioning from heavily slowed feature to fast perimeter printing."
    );
    def->units = {L("mm")};
    def->min      = 0;
    def->init_fn  = init_with(0.);

    def = defs.add("cooling_tube_retraction", typeid(double));
    def->location = Printer;
    def->label = L("Cooling tube position");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Distance of the center-point of the cooling tube from the extruder tip.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(91.5);

    def = defs.add("cooling_tube_length", typeid(double));
    def->location = Printer;
    def->label = L("Cooling tube length");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Length of the cooling tube to limit space for cooling moves inside it.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(5.);

    def = defs.add("default_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Default");
    def->tooltip = L("This is the acceleration your printer will be reset to after "
                   "the role-specific acceleration values are used (perimeter/infill). "
                   "Set zero to prevent resetting acceleration at all.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    /* TODO: What about these?
    def = defs.add("default_filament_profile", typeid(std::vector<std::string>));
    def->label = L("Default filament profile");
    def->tooltip = L("Default filament profile associated with the current printer profile. "
                   "On selection of the current printer profile, this filament profile will be activated.");
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("default_print_profile", typeid(std::string));
    def->label = L("Default print profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->init_fn = SET_DEFAULT( new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("disable_fan_first_layers", typeid(int));
    def->location = Filament;
    def->label = L("Disable fan for the first");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FirstLayers;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("You can set this to a positive value to disable fan at all "
                   "during the first layers, so that it does not make adhesion worse.");
    def->units = {L("layers")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(3);

    def = defs.add("dont_support_bridges", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Don't support bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Experimental option for preventing support material from being generated "
                   "under bridged areas.");
    def->init_fn = init_with(true);

    /* TODO: What is this?
    def = defs.add("duplicate_distance", typeid(double));
    def->label = L("Distance between copies");
    def->tooltip = L("Distance used for the auto-arrange feature of the plater.");
    def->sidetext = L("mm");
    def->aliases = { "multiply_distance" };
    def->min = 0;
    def->init_fn = SET_DEFAULT(6);*/

    def = defs.add("end_gcode", typeid(std::string));
    def->location = Printer;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 2;
    def->label = L("End G-code");
    def->tooltip = L("This end procedure is inserted at the end of the output file. "
                   "Note that you can use placeholder variables for all PiggieSlicer settings.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n");



    /////////////////////////////////////////////////////



    
    def = defs.add("end_filament_gcode", typeid(std::string));
    def->location = Filament;
    def->label = L("End G-code");
    def->category = ConfigItemDef::Category::Filament_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Filament_CustomGCode;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This end procedure is inserted at the end of the output file, before the printer end gcode (and "
                   "before any toolchange from this filament in case of multimaterial printers). "
                   "Note that you can use placeholder variables for all PiggieSlicer settings. "
                   "If you have multiple extruders, the gcode is processed in extruder order.");
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->init_fn = init_with("; Filament-specific end gcode \n;END gcode for filament\n");

    def = defs.add("ensure_vertical_shell_thickness", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ensure vertical shell thickness");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Add solid infill near sloping surfaces to guarantee the vertical shell thickness "
                   "(top+bottom solid layers).");
    def->init_fn = init_with(
        EnsureVerticalShellThickness::Enabled,
        {{int(EnsureVerticalShellThickness::Disabled), "disabled", L("Disabled")},
         {int(EnsureVerticalShellThickness::Partial), "partial", L("Partial")},
         {int(EnsureVerticalShellThickness::Enabled), "enabled", L("Enabled")}}
    );

    def = defs.add("top_fill_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Top fill pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->order = 0;
    def->tooltip = L("Fill pattern for top infill. This only affects the top visible layer, and not its adjacent solid shells.");
    def->cli = "top-fill-pattern|external-fill-pattern|solid-fill-pattern";
    // solid_fill_pattern is an obsolete equivalent to top_fill_pattern/bottom_fill_pattern.
    def->aliases = { "solid_fill_pattern", "external_fill_pattern" };
    def->init_fn = init_with(
        InfillPattern::ipMonotonic,
        {{int(InfillPattern::ipRectilinear), "rectilinear", L("Rectilinear")},
         {int(InfillPattern::ipMonotonic), "monotonic", L("Monotonic")},
         {int(InfillPattern::ipMonotonicLines), "monotoniclines", L("Monotonic Lines")},
         {int(InfillPattern::ipAlignedRectilinear), "alignedrectilinear", L("Aligned Rectilinear")},
         {int(InfillPattern::ipConcentric), "concentric", L("Concentric")},
         {int(InfillPattern::ipHilbertCurve), "hilbertcurve", L("Hilbert Curve")},
         {int(InfillPattern::ipArchimedeanChords), "archimedeanchords", L("Archimedean Chords")},
         {int(InfillPattern::ipOctagramSpiral), "octagramspiral", L("Octagram Spiral")},
         {int(InfillPattern::ipSpiralInset), "spiralinset", L("Spiral Inset")}}
    );

    def = defs.add("bottom_fill_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Bottom fill pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Fill pattern for bottom infill. This only affects the bottom external visible layer, and not its adjacent solid shells.");
    def->cli = "bottom-fill-pattern|external-fill-pattern|solid-fill-pattern";
    def->aliases = { "solid_fill_pattern", "external_fill_pattern" };
    def->init_fn = init_with(
        InfillPattern::ipMonotonic,
        {{int(InfillPattern::ipRectilinear), "rectilinear", L("Rectilinear")},
         {int(InfillPattern::ipMonotonic), "monotonic", L("Monotonic")},
         {int(InfillPattern::ipMonotonicLines), "monotoniclines", L("Monotonic Lines")},
         {int(InfillPattern::ipAlignedRectilinear), "alignedrectilinear", L("Aligned Rectilinear")},
         {int(InfillPattern::ipConcentric), "concentric", L("Concentric")},
         {int(InfillPattern::ipHilbertCurve), "hilbertcurve", L("Hilbert Curve")},
         {int(InfillPattern::ipArchimedeanChords), "archimedeanchords", L("Archimedean Chords")},
         {int(InfillPattern::ipOctagramSpiral), "octagramspiral", L("Octagram Spiral")},
         {int(InfillPattern::ipSpiralInset), "spiralinset", L("Spiral Inset")}}
    );

    def = defs.add("small_area_infill_flow_compensation", typeid(bool));
    def->location = Print;
    def->label = L("Small area flow compensation (beta)");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Reduce the flow of short solid infill lines (rectilinear and monotonic patterns) following the model "
                     "below, against over-extruded small areas.");
    def->init_fn = init_with(false);

    def = defs.add("small_area_infill_flow_compensation_model", typeid(std::string));
    def->location = Print;
    def->label = L("Flow Compensation Model");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Flow Compensation Model, used to adjust the flow for small infill areas. The model is expressed as "
                     "a comma separated pair of values for extrusion length and flow correction factor. Each pair is on a "
                     "separate line, followed by a semicolon, in the following format: \"1.234, 5.678;\"");
    def->multiline = true;
    def->full_width = true;
    def->height = 10;
    def->init_fn = init_with(std::string{"0,0\n0.2,0.4444\n0.4,0.6145\n0.6,0.7059\n0.8,0.7619\n1.5,0.8571\n2,0.8889\n3,0.9231\n5,0.9520\n10,1"});

    def = defs.add("detect_narrow_internal_solid_infill", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Detect narrow internal solid infills");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 13;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This option will auto-detect narrow internal solid infill areas. If enabled, the concentric pattern "
                     "will be used for the area to speed up printing. Otherwise, the rectilinear pattern will be used by "
                     "default.");
    def->init_fn = init_with(true);

    def = defs.add("internal_solid_infill_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Internal solid infill pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Line pattern of internal solid infill. if the detect narrow internal solid infill be enabled, the "
                     "concentric pattern will be used for the small area.");
    def->init_fn = init_with(
        InfillPattern::ipEnsuring,
        {{int(InfillPattern::ipRectilinear), "rectilinear", L("Rectilinear")},
         {int(InfillPattern::ipMonotonic), "monotonic", L("Monotonic")},
         {int(InfillPattern::ipMonotonicLines), "monotoniclines", L("Monotonic Lines")},
         {int(InfillPattern::ipAlignedRectilinear), "alignedrectilinear", L("Aligned Rectilinear")},
         {int(InfillPattern::ipConcentric), "concentric", L("Concentric")},
         {int(InfillPattern::ipHilbertCurve), "hilbertcurve", L("Hilbert Curve")},
         {int(InfillPattern::ipArchimedeanChords), "archimedeanchords", L("Archimedean Chords")},
         {int(InfillPattern::ipOctagramSpiral), "octagramspiral", L("Octagram Spiral")},
         {int(InfillPattern::ipEnsuring), "ensuring", L("Ensuring")},
         {int(InfillPattern::ipSpiralInset), "spiralinset", L("Spiral Inset")}}
    );

    def = defs.add("top_surface_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Top surface density");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Density of top surface layer. A value of 100% creates a fully solid, smooth top layer. Reducing "
                     "this value results in a textured top surface, according to the chosen top surface pattern. A value "
                     "of 0% will result in only the walls on the top layer being created. Intended for aesthetic or "
                     "functional purposes, not to fix issues such as over-extrusion.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("top_surface_fill_order", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Top surface fill order");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Direction in which top surfaces are filled when using a center-based pattern (Concentric, Spiral "
                     "Inset, Archimedean Chords, Octagram Spiral). Outward starts at the center of the surface, so any "
                     "excess material is pushed towards the edge where it is least visible. Inward starts at the edge and "
                     "ends with the tight curves at the center. Default uses shortest-path ordering, which may run in "
                     "either direction.");
    def->init_fn = init_with(
        SurfaceFillOrder::Default,
        {{int(SurfaceFillOrder::Default), "default", L("Default")},
         {int(SurfaceFillOrder::Outward), "outward", L("Outward")},
         {int(SurfaceFillOrder::Inward), "inward", L("Inward")}}
    );

    def = defs.add("bottom_surface_fill_order", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Bottom surface fill order");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Direction in which bottom surfaces are filled when using a center-based pattern (Concentric, Spiral "
                     "Inset, Archimedean Chords, Octagram Spiral). Inward starts each surface with the wider outer "
                     "curves, which improves first layer adhesion on build plates where the tight curves at the center "
                     "may not stick. Outward starts at the center, pushing any excess material towards the edge. Default "
                     "uses shortest-path ordering, which may run in either direction.");
    def->init_fn = init_with(
        SurfaceFillOrder::Default,
        {{int(SurfaceFillOrder::Default), "default", L("Default")},
         {int(SurfaceFillOrder::Outward), "outward", L("Outward")},
         {int(SurfaceFillOrder::Inward), "inward", L("Inward")}}
    );

    def = defs.add("center_of_surface_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Center surface pattern on");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Chooses where the centering point of centered top/bottom surface patterns (Archimedean Chords, "
                     "Octagram Spiral) is placed. - Each Surface: centers the pattern on every individual surface region, "
                     "so each island is symmetric on its own. - Each Model: centers the pattern on each connected body. "
                     "Parts that touch or overlap share one center; parts detached from the rest each get their own. - "
                     "Each Assembly: uses a single shared center for the whole object or assembly.");
    def->init_fn = init_with(
        CenterOfSurfacePattern::EachSurface,
        {{int(CenterOfSurfacePattern::EachSurface), "each_surface", L("Each Surface")},
         {int(CenterOfSurfacePattern::EachModel), "each_model", L("Each Model")},
         {int(CenterOfSurfacePattern::EachAssembly), "each_assembly", L("Each Assembly")}}
    );

    def = defs.add("top_surface_expansion", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Top surface expansion");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Expands the top surfaces by this distance to connect distinct top surfaces and fill gaps. Useful "
                     "for cases where the top surface is interrupted by a raised feature, such as text on a plane. "
                     "Expanding it removes the holes beneath these features and creates a continuous path with a better "
                     "finish for printing on top. The expansion is applied to the original top surface, before any other "
                     "processing such as bridging or overhang detection.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("top_surface_expansion_margin", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Top expansion wall margin");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 11;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Using “Top surface expansion” may cause a surface that did not previously touch the model's outer "
                     "walls to now do so. This can cause contraction marks (such as the hull line) on the outer walls. By "
                     "adding a small margin, this contraction will not occur directly on the walls, thereby preventing a "
                     "visible mark.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 10;
    def->init_fn = init_with(0.);

    def = defs.add("top_surface_expansion_direction", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Top expansion direction");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 12;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Direction in which the top surface expansion grows. - Inward grows into the holes and gaps left by "
                     "features rising from the middle of a top surface. - Outward grows the outer edge of the surface, "
                     "connecting surfaces separated by features that can divide a surface, such as a lattice pattern. - "
                     "Inward and Outward does both.");
    def->init_fn = init_with(
        TopSurfaceExpansionDirection::InwardAndOutward,
        {{int(TopSurfaceExpansionDirection::InwardAndOutward), "inward_and_outward", L("Inward and Outward")},
         {int(TopSurfaceExpansionDirection::Inward), "inward", L("Inward")},
         {int(TopSurfaceExpansionDirection::Outward), "outward", L("Outward")}}
    );

    def = defs.add("bottom_surface_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Bottom surface density");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Density of the bottom surface layer. Intended for aesthetic or functional purposes, not to fix "
                     "issues such as over-extrusion. WARNING: Lowering this value may negatively affect bed adhesion.");
    def->units = {L("%")};
    def->min = 10;
    def->max = 100;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("external_perimeter_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("External perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for external perimeters. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 200%), it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("external_perimeter_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("External perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("This separate setting will affect the speed of external perimeters (the visible ones). "
                   "If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{50.}));
    def->ratio_over = "perimeter_speed";

    // Orca wall_sequence; replaces 3.0's external_perimeters_first (read into it from old configs)
    def = defs.add("wall_sequence", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Walls printing order");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Print sequence of the internal (inner) and external (outer) walls.\n\n"
                     "Use Inner/Outer for best overhangs. This is because the overhanging walls can adhere to a neighbouring perimeter while printing. "
                     "However, this option results in slightly reduced surface quality as the external perimeter is deformed by being squashed to the internal perimeter.\n\n"
                     "Use Inner/Outer/Inner for the best external surface finish and dimensional accuracy as the external wall is printed undisturbed from an internal perimeter. "
                     "However, overhang performance will reduce as there is no internal perimeter to print the external wall against. "
                     "This option requires a minimum of 3 walls to be effective as it prints the internal walls from the 3rd perimeter onwards first, "
                     "then the external perimeter and, finally, the first internal perimeter. "
                     "This option is recommended over the Outer/Inner option in most cases.\n\n"
                     "Use Outer/Inner for the same external wall quality and dimensional accuracy benefits of Inner/Outer/Inner option. "
                     "However, the Z seams will appear less consistent as the first extrusion of a new layer starts on a visible surface.");
    def->init_fn = init_with(
        WallSequence::InnerOuter,
        {{int(WallSequence::InnerOuter), "inner wall/outer wall", L("Inner/Outer")},
         {int(WallSequence::OuterInner), "outer wall/inner wall", L("Outer/Inner")},
         {int(WallSequence::InnerOuterInner), "inner-outer-inner wall", L("Inner/Outer/Inner")}}
    );

    def = defs.add("extra_perimeters", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Extra perimeters if needed");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Add more perimeters when needed for avoiding gaps in sloping walls. "
                   "Slic3r keeps adding perimeters, until more than 70% of the loop immediately above "
                   "is supported.");
    def->init_fn = init_with(true);

    def = defs.add("extra_perimeters_on_overhangs", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Extra perimeters on overhangs (Experimental)");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Detect overhang areas where bridges cannot be anchored, and fill them with "
                    "extra perimeter paths. These paths are anchored to the nearby non-overhang area when possible.");
    def->init_fn = init_with(false);

    def = defs.add("extruder", typeid(int));
    def->location = Object;
    def->overrides_in = Locations{ Volume };
    def->label = L("Extruder");
    def->category = ConfigItemDef::Category::Object_Extruders;
    def->gui_type = ConfigItemDef::GUIType::extruder_selection;
    def->tooltip = L("The extruder to use (unless more specific extruder settings are specified). "
                   "This value overrides perimeter and infill extruders, but not the support extruders.");
    def->min = 0;  // 0 = inherit defaults
    def->init_fn = init_with(0);

    def = defs.add("extruder_colour", typeid(std::vector<std::string>));
    def->location = Project;
    def->label = "Extruder Color";
    def->category = ConfigItemDef::Category::Hidden;
    def->gui_type = ConfigItemDef::GUIType::color;
    def->init_fn = init_with(std::vector<std::string>{});

    def = defs.add("extruder_offset", typeid(std::vector<Vec2d>));
    def->location = Printer;
    def->gui_type = ConfigItemDef::GUIType::points;
    def->option_group = ConfigItemDef::OptionGroup::Printer_MultipleExtruder_Position;
    def->category = ConfigItemDef::Category::Printer_MultipleExtruders;
    def->label = L("Extruder offset");
    def->tooltip = L("If your firmware doesn't handle the extruder displacement you need the G-code "
                   "to take it into account. This option lets you specify the displacement of each extruder "
                   "with respect to the first one. It expects positive coordinates (they will be subtracted "
                   "from the XY coordinate).");
    def->units = {L("mm")};
    def->init_fn = init_with(std::vector{Vec2d(0,0)});
    def->require_tool_parity = true;

    /* TODO: shouldn't we remove this crap?
    def = defs.add("extrusion_axis", typeid(std::string));
    def->label = L("Extrusion axis");
    def->tooltip = L("Use this option to set the axis letter associated to your printer's extruder "
                   "(usually E but some printers use A).");
    def->init_fn = SET_DEFAULT("E"));*/

    def = defs.add("extruder_clearance_height", typeid(double));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_SequentialPrintingLimits;
    def->order = 0;
    def->label = L("Height");
    def->tooltip = L("Only used when 'Print Settings -> Complete individual objects' is active. Set this to the vertical "
                   "distance between your nozzle tip and (usually) the X carriage rods. Used to check for collisions "
                   "with previously printed objects and to prevent them when arranging.\n"
                   "The value is ignored for most Prusa printers, which come with more detailed extruder model.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(20.);

    def = defs.add("extruder_clearance_height_to_lid", typeid(double));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_SequentialPrintingLimits;
    def->order = 2;
    def->label = L("Height to lid");
    def->tooltip = L("Distance from the nozzle tip to the lid. With 'Complete individual objects', only the last "
                     "object printed may be taller. 0: no lid. Ignored for printers with a detailed extruder model.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(120.);

    def = defs.add("extruder_clearance_radius", typeid(double));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_SequentialPrintingLimits;
    def->order = 1;
    def->label = L("Radius");
    def->tooltip = L("Only used when 'Print Settings -> Complete individual objects' is active. Set this to a radius "
                     "of a nozzle-centered cylinder big enough to enclose the extruder assembly. Used to check for collisions "
                     "with previously printed objects and to prevent them when arranging.\n"
                     "The value is ignored for most Prusa printers, which come with more detailed extruder model.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(20.);    

    def = defs.add("extrusion_multiplier", typeid(double));
    def->location = Filament;
    def->label = L("Extrusion multiplier");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_ExtrusionCalibration;
    def->category = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor changes the amount of flow proportionally. You may need to tweak "
                   "this setting to get nice surface finish and correct single wall widths. "
                   "Usual values are between 0.9 and 1.1. If you think you need to change this more, "
                   "check filament diameter and your firmware E steps.");
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Default extrusion width");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to allow a manual extrusion width. "
                   "If left to zero, Slic3r derives extrusion widths from the nozzle diameter "
                   "(see the tooltips for perimeter extrusion width, infill extrusion width etc). "
                   "If expressed as percentage (for example: 230%), it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max = 1000;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("fan_always_on", typeid(bool));
    def->location = Filament;
    def->label = L("Keep fan always on");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If this is enabled, fan will never be disabled and will be kept running at least "
                   "at its minimum speed. Useful for PLA, harmful for ABS.");
    def->init_fn = init_with(false);

    def = defs.add("fan_below_layer_time", typeid(int));
    def->location = Filament;
    def->label = L("Enable fan if layer print time is below");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingThresholds;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("If layer print time is estimated below this number of seconds, fan will be enabled "
                   "and its speed will be calculated by interpolating the minimum and maximum speeds.");
    def->units = {L("approximate seconds")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(60);

    def = defs.add("filament_change_time", typeid(double));
    def->location = Printer;
    def->label = L("Filament change time");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 5;
    def->tooltip = L("Time required for a single filament change. On a printer with"
         " multiple tools this is the time required for a single toolchange to take place."
         " On a printer with multi-material upgrade, this is the time required"
         " to unload and load a new filament.");
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->min = 0.0;
    def->units = {L("s")};
    def->init_fn = init_with(0.0);

    def = defs.add("filament_colour", typeid(std::string));
    def->location = Filament;
    def->label = L("Color");
    // TODO: This option is temporarily hidden from the UI, until we handle it properly.
    // The plan is to make it std::optional<std::string>. Currently it is used as
    // a fallback, but once it is overridden at project level, it cannot be restored
    // (there is no UI to do it).
    //def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Hidden; //Filament_MaterialTemperatures;
    def->order = 1;
    def->tooltip = L("Leave unused if the filament profile isn't tied to a specific color. "
                     "Color can still be set at project-level.");
    def->gui_type = ConfigItemDef::GUIType::color;
    def->init_fn = init_with("#29B2B2");

    def = defs.add("filament_notes", typeid(std::string));
    def->location = Filament;
    def->label = L("Filament notes");
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Filament_Notes;
    def->order = 2;
    def->option_group = ConfigItemDef::OptionGroup::Filament_Notes_Notes;
    def->tooltip = L("You can put your notes regarding the filament here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->init_fn = init_with("");

    def = defs.add("filament_max_volumetric_speed", typeid(double));
    def->location = Filament;
    def->label = L("Max volumetric speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Overrides_PrintSpeedOverride;
    def->category = ConfigItemDef::Category::Filament_Overrides;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum volumetric speed allowed for this filament. Limits the maximum volumetric "
                   "speed of a print to the minimum of print and filament volumetric speed. "
                   "Set to zero for no limit.");
    def->units = {L("mm³/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_adaptive_volumetric_speed", typeid(bool));
    def->location = Filament;
    def->label = L("Adaptive volumetric speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Overrides_PrintSpeedOverride;
    def->category = ConfigItemDef::Category::Filament_Overrides;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("When enabled, the extrusion flow is limited by the smaller of the fitted value (calculated from "
                     "line width and layer height) and the user-defined maximum flow. When disabled, only the "
                     "user-defined maximum flow is applied. Note: Experimental and incomplete feature imported from BBS. "
                     "Functional for some profiles that already have the variable saved.");
    def->init_fn = init_with(false);

    def = defs.add("volumetric_speed_coefficients", typeid(std::string));
    def->location = Filament;
    def->label = L("Max volumetric speed multinomial coefficients");
    def->category = ConfigItemDef::Category::Hidden;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Six coefficients of the fitted max volumetric speed: c0·h² + c1·w² + c2·h·w + c3·h + c4·w + c5 "
                     "(h layer height, w line width).");
    def->init_fn = init_with("");

    def = defs.add("filament_infill_max_speed", typeid(double));
    def->location = Filament;
    def->label = L("Max non-crossing infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Overrides_PrintSpeedOverride;
    def->category = ConfigItemDef::Category::Filament_Overrides;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum speed allowed for this filament while printing infill without "
                     "any self intersections in a single layer. "
                     "Set to zero for no limit.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_infill_max_crossing_speed", typeid(double));
    def->location = Filament;
    def->label = L("Max crossing infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Overrides_PrintSpeedOverride;
    def->category = ConfigItemDef::Category::Filament_Overrides;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum speed allowed for this filament while printing infill with "
                     "self intersections in a single layer. "
                     "Set to zero for no limit.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_loading_speed", typeid(double));
    def->location = Filament;
    def->label = L("Loading speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MovementTiming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used for loading the filament on the wipe tower.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(28.);

    def = defs.add("filament_loading_speed_start", typeid(double));
    def->location = Filament;
    def->label = L("Loading speed at the start");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MovementTiming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used at the very beginning of loading phase.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(3.);

    def = defs.add("filament_unloading_speed", typeid(double));
    def->location = Filament;
    def->label = L("Unloading speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MovementTiming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used for unloading the filament on the wipe tower (does not affect "
                      " initial part of unloading just after ramming).");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(90.);

    def = defs.add("filament_unloading_speed_start", typeid(double));
    def->location = Filament;
    def->label = L("Unloading speed at the start");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MovementTiming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used for unloading the tip of the filament immediately after ramming.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(100.);

    def = defs.add("filament_toolchange_delay", typeid(double));
    def->location = Filament;
    def->label = L("Delay after unloading");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Time to wait after the filament is unloaded. "
                   "May help to get reliable toolchanges with flexible materials "
                   "that may need more time to shrink to original dimensions.");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_stamping_loading_speed", typeid(double));
    def->location = Filament;
    def->label = L("Stamping loading speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used for stamping.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(20.);

    def = defs.add("filament_stamping_distance", typeid(double));
    def->location = Filament;
    def->label = L("Stamping distance measured from the center of the cooling tube");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("If set to nonzero value, filament is moved toward the nozzle between the individual cooling moves (\"stamping\"). "
                     "This option configures how long this movement should be before the filament is retracted again.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_cooling_moves", typeid(int));
    def->location = Filament;
    def->label = L("Number of cooling moves");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Filament is cooled by being moved back and forth in the "
                   "cooling tubes. Specify desired number of these moves.");
    def->max = 0;
    def->max = 20;
    def->init_fn = init_with(4);

    def = defs.add("filament_cooling_initial_speed", typeid(double));
    def->location = Filament;
    def->label = L("Speed of the first cooling move");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Cooling moves are gradually accelerating beginning at this speed.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(2.2);

    def = defs.add("filament_minimal_purge_on_wipe_tower", typeid(double));
    def->location = Filament;
    def->label = L("Minimal purge on wipe tower");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_WipeTowerPurging;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("After a tool change, the exact position of the newly loaded filament inside "
                     "the nozzle may not be known, and the filament pressure is likely not yet stable. "
                     "Before purging the print head into an infill or a sacrificial object, Slic3r will always prime "
                     "this amount of material into the wipe tower to produce successive infill or sacrificial object extrusions reliably.");
    def->units = {L("mm³")};
    def->min = 0;
    def->init_fn = init_with(15.);

    def = defs.add("filament_cooling_final_speed", typeid(double));
    def->location = Filament;
    def->label = L("Speed of the last cooling move");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Cooling moves are gradually accelerating towards this speed.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(3.4);

    def = defs.add("filament_purge_multiplier", typeid(Percentage));
    def->location = Filament;
    def->label = L("Purge volume multiplier");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_WipeTowerPurging;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Purging volume on the wipe tower is determined by 'multimaterial_purging' in Printer Settings. "
                     "This option allows to modify the volume on filament level. "
                     "Note that the project can override this by setting project-specific values.");
    def->units = {L("%")};
    def->min = 0;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("filament_ramming_parameters", typeid(std::string));
    def->location = Filament;
    def->label = L("Ramming parameters");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::ramming_params;
    def->tooltip = L("This string is edited by RammingDialog and contains ramming specific parameters.");
    def->init_fn = init_with("120 100 6.6 6.8 7.2 7.6 7.9 8.2 8.7 9.4 9.9 10.0|"
       " 0.05 6.6 0.45 6.8 0.95 7.8 1.45 8.3 1.95 9.7 2.45 10 2.95 7.6 3.45 7.6 3.95 7.6 4.45 7.6 4.95 7.6");

    def = defs.add("filament_ramming_temperature_delta", typeid(int));
    def->location = Filament;
    def->label = L("Ramming temperature variation");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Temperature difference to be applied right before ramming. The value can be negative.");
    def->units = {"∆°C"};
    def->init_fn = init_with(0);

    def = defs.add("filament_ramming_initial_delay", typeid(double));
    def->location = Filament;
    def->label = L("Pause before ramming");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->units = {L("s")};
    def->tooltip = L("Time in seconds that the printer will remain idle before ramming, allowing the melt zone temperature to equalize.");
    def->init_fn = init_with(0.0);

    def = defs.add("filament_multitool_ramming", typeid(bool));
    def->location = Filament;
    def->label = L("Enable ramming for multitool setups");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Perform ramming when using multitool printer (i.e. when the 'Single Extruder Multimaterial' in Printer Settings is unchecked). "
                     "When checked, a small amount of filament is rapidly extruded on the wipe tower just before the toolchange. "
                     "This option is only used when the wipe tower is enabled.");
    def->init_fn = init_with(false);

    def = defs.add("filament_multitool_ramming_volume", typeid(double));
    def->location = Filament;
    def->label = L("Multitool ramming volume");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The volume to be rammed before the toolchange.");
    def->units = {L("mm³")};
    def->min = 0;
    def->init_fn = init_with(10.);

    def = defs.add("filament_multitool_ramming_flow", typeid(double));
    def->location = Filament;
    def->label = L("Multitool ramming flow");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Flow used for ramming the filament before the toolchange.");
    def->units = {L("mm³/s")};
    def->min = 0;
    def->init_fn = init_with(10.);

    def               = defs.add("filament_flush_volume", typeid(double));
    def->location     = Filament;
    def->label        = L("Flush volume");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_FlushParameters;
    def->category     = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order        = 0;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip =
        L("Volume of filament to flush during a tool change. "
          "Used in custom toolchange G-code.");
    def->units = {L("mm³")};
    def->min      = 0;
    def->init_fn  = init_with(0.);

    def               = defs.add("filament_flush_speed", typeid(double));
    def->location     = Filament;
    def->label        = L("Flush speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_FlushParameters;
    def->category     = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order        = 1;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip =
        L("Extrusion speed used for flushing during a tool change. "
          "Used in custom toolchange G-code.");
    def->units = {L("mm/s")};
    def->min      = 0;
    def->init_fn  = init_with(0.);

    def = defs.add("filament_diameter", typeid(double));
    def->location = Filament;
    def->label = L("Diameter");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_ExtrusionCalibration;
    def->category = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Enter your filament diameter here. Good precision is required, so use a caliper "
                   "and do multiple measurements along the filament, then compute the average.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(1.75);

    def = defs.add("filament_density", typeid(double));
    def->location = Filament;
    def->label = L("Density");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Enter your filament density here. This is only for statistical information. "
                   "A decent way is to weigh a known length of filament and compute the ratio "
                   "of the length to volume. Better is to calculate the volume directly through displacement.");
    def->units = {L("g/cm³")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_type", typeid(std::string));
    def->location = Filament;
    def->label = L("Filament type");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::s_enum_open;
    def->tooltip = L("The filament material type for use in custom G-codes.");
    def->choices = {
        { std::string("PLA"),  std::string("PLA")   },
        { std::string("PET"),  std::string("PET")   },
        { std::string("ABS"),  std::string("ABS")   },
        { std::string("ASA"),  std::string("ASA")   },
        { std::string("FLEX"), std::string("FLEX")  },
        { std::string("HIPS"), std::string("HIPS")  },
        { std::string("EDGE"), std::string("EDGE")  },
        { std::string("NGEN"), std::string("NGEN")  },
        { std::string("PA"),   std::string("PA")    },
        { std::string("NYLON"),std::string("NYLON") },
        { std::string("PVA"),  std::string("PVA")   },
        { std::string("PC"),   std::string("PC")    },
        { std::string("PP"),   std::string("PP")    },
        { std::string("PEI"),  std::string("PEI")   },
        { std::string("PEEK"), std::string("PEEK")  },
        { std::string("PEKK"), std::string("PEKK")  },
        { std::string("POM"),  std::string("POM")   },
        { std::string("PSU"),  std::string("PSU")   },
        { std::string("PVDF"), std::string("PVDF")  },
        { std::string("SCAFF"),std::string("SCAFF") },
    };
    def->init_fn = init_with("PLA");

    def = defs.add("filament_soluble", typeid(bool));
    def->location = Filament;
    def->label = L("Soluble material");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Soluble material is most likely used for a soluble support.");
    def->init_fn = init_with(false);

    def = defs.add("filament_abrasive", typeid(bool));
    def->location = Filament;
    def->label = L("Abrasive material");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This flag means that the material is abrasive and requires a hardened nozzle. The value is used by the printer to check it.");
    def->init_fn = init_with(false);

    def = defs.add("filament_cost", typeid(double));
    def->location = Filament;
    def->label = L("Cost");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Enter your filament cost per kg here. This is only for statistical information.");
    def->units = {L("money/kg")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_spool_weight", typeid(double));
    def->location = Filament;
    def->label = L("Spool weight");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Enter weight of the empty filament spool. "
                     "One may weigh a partially consumed filament spool before printing and one may compare the measured weight "
                     "with the calculated weight of the filament with the spool to find out whether the amount "
                     "of filament on the spool is sufficient to finish the print.");
    def->units = {L("g")};
    def->min = 0;
    def->init_fn = init_with(0.);

    /* TODO: What about this?
    def = defs.add("filament_settings_id", typeid(std::vector<std::string>));
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings { "" });
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("filament_vendor", typeid(std::string));
    def->location = Filament;
    def->category = ConfigItemDef::Category::Hidden;
    def->cli = ConfigItemDef::nocli;
    def->init_fn = init_with(L("(Unknown)"));

    def = defs.add("filament_shrinkage_compensation_xy", typeid(Percentage));
    def->location = Filament;
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_Compensation;
    def->category = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Shrinkage compensation XY");
    def->tooltip = L("Enter your filament shrinkage percentages for the X and Y axes here to apply scaling of the object to "
                     "compensate for shrinkage in the X and Y axes. For example, if you measured 99mm instead of 100mm, "
                     "enter 1%.");
    def->units = {L("%")};
    def->min = -10.;
    def->max = 10.;
    def->init_fn = init_with(Percentage{0.});

    def = defs.add("filament_shrinkage_compensation_z", typeid(Percentage));
    def->location = Filament;
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_Compensation;
    def->category = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Shrinkage compensation Z");
    def->tooltip = L("Enter your filament shrinkage percentages for the Z axis here to apply scaling of the object to "
                     "compensate for shrinkage in the Z axis. For example, if you measured 99mm instead of 100mm, "
                     "enter 1%.");
    def->units = {L("%")};
    def->min = -10.;
    def->max = 10.;
    def->init_fn = init_with(Percentage{0.});

    def = defs.add("fill_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fill angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Default base angle for infill orientation. Cross-hatching will be applied to this. "
                   "Bridges will be infilled using the best direction Slic3r can detect, so this setting "
                   "does not affect them.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 360;
    def->init_fn = init_with(45.);

    def = defs.add("solid_infill_direction", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Solid infill direction");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Angle for solid infill pattern, which controls the start or main direction of line.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 360;
    def->init_fn = init_with(45.);

    def = defs.add("top_layer_direction", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Top layer direction");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Fixed angle for the top solid infill and ironing lines. Set to -1 to follow the default solid "
                     "infill direction.");
    def->units = {L("°")};
    def->min = -1;
    def->max = 360;
    def->init_fn = init_with(-1.);

    def = defs.add("bottom_layer_direction", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Bottom layer direction");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Fixed angle for the bottom solid infill lines. Set to -1 to follow the default solid infill "
                     "direction.");
    def->units = {L("°")};
    def->min = -1;
    def->max = 360;
    def->init_fn = init_with(-1.);

    def = defs.add("fill_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Fill density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("Density of internal infill, expressed in the range 0% - 100%.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->choices = {
        { 0., "0%" },
        { 5., "5%" },
        { 10., "10%" },
        { 15., "15%" },
        { 20., "20%" },
        { 25., "25%" },
        { 30., "30%" },
        { 40., "40%" },
        { 50., "50%" },
        { 60., "60%" },
        { 70., "70%" },
        { 80., "80%" },
        { 90., "90%" },
        { 100., "100%" }
    };
    def->init_fn = init_with(Percentage{20.});

    def = defs.add("lightning_overhang_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Lightning overhang angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum overhang angle for Lightning infill support propagation.");
    def->units = {L("°")};
    def->min = 5;
    def->max = 85;
    def->init_fn = init_with(45.);

    def = defs.add("lightning_prune_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Prune angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Controls how aggressively short or unsupported Lightning branches are pruned. This angle is "
                     "converted internally to a per-layer distance.");
    def->units = {L("°")};
    def->min = 5;
    def->max = 85;
    def->init_fn = init_with(45.);

    def = defs.add("lightning_straightening_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Straightening angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum straightening angle used to simplify Lightning branches.");
    def->units = {L("°")};
    def->min = 5;
    def->max = 85;
    def->init_fn = init_with(45.);

    def = defs.add("infill_complete_top", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fill pattern tops");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Choose this option if you want to completely fill in the tops of the infill pattern");
    def->init_fn = init_with(false);

    def = defs.add("lateral_lattice_angle_1", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Lateral lattice angle 1");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The angle of the first set of Lateral lattice elements in the Z direction. Zero is vertical.");
    def->units = {L("°")};
    def->min = -75;
    def->max = 75;
    def->init_fn = init_with(-45.);

    def = defs.add("lateral_lattice_angle_2", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Lateral lattice angle 2");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The angle of the second set of Lateral lattice elements in the Z direction. Zero is vertical.");
    def->units = {L("°")};
    def->min = -75;
    def->max = 75;
    def->init_fn = init_with(45.);

    def = defs.add("infill_overhang_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Infill overhang angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The angle of the infill angled lines. 60° will result in a pure honeycomb.");
    def->units = {L("°")};
    def->min = 15;
    def->max = 75;
    def->init_fn = init_with(60.);

    def = defs.add("tpms_adaptive", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Adaptive density (experimental)");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 11;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Grades the Gyroid and TPMS infill inside the object: its cells grow from the surface of the "
                     "object towards its center. The sparse infill density is used at the surface and the interior "
                     "density at the center.\n"
                     "Distance warp, Smooth blend and Stepped shells follow the distance to the nearest surface, "
                     "including the top and bottom, with the interior density at the point farthest from it:\n"
                     " - Distance warp: one continuous pattern, stretched and sheared where the distance changes "
                     "across directions, as in plates and long parts.\n"
                     " - Smooth blend: the patterns of neighbouring densities blended into each other, with small "
                     "loops where they meet.\n"
                     " - Stepped shells: shells of the regular pattern at densities about 1.5 times apart, their "
                     "lines joined along the shell boundaries.\n"
                     " - Lobes: follows the 3D shape of the object, including its top and bottom. Every lobe, a part "
                     "joined to the rest by a narrower neck, is graded towards its own center.\n"
                     " - Normal Z, Y or X: follows the sections of the object normal to that axis, so the density "
                     "does not change along it.");
    def->init_fn = init_with(
        TpmsAdaptiveMode::Disabled,
        {{int(TpmsAdaptiveMode::Disabled), "disabled", L("Disabled")},
         {int(TpmsAdaptiveMode::DistanceWarp), "distance_warp", L("Distance warp")},
         {int(TpmsAdaptiveMode::SmoothBlend), "smooth_blend", L("Smooth blend")},
         {int(TpmsAdaptiveMode::SteppedShells), "stepped_shells", L("Stepped shells")},
         {int(TpmsAdaptiveMode::Lobes), "lobes", L("Lobes")},
         {int(TpmsAdaptiveMode::NormalZ), "normal_z", L("Normal Z")},
         {int(TpmsAdaptiveMode::NormalY), "normal_y", L("Normal Y")},
         {int(TpmsAdaptiveMode::NormalX), "normal_x", L("Normal X")}}
    );

    def = defs.add("tpms_interior_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Interior density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 12;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Density of the adaptive infill at the center of the object.");
    def->units = {L("%")};
    def->min = 1;
    def->max = 100;
    def->init_fn = init_with(Percentage{5.});

    def = defs.add("tpms_adaptive_gradient", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Adaptive gradient");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 13;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("How the density changes from the surface to the center of the object.\n"
                     "Linear: the density changes at a constant rate.\n"
                     "Quadratic: the density stays close to the sparse infill density near the surface and "
                     "changes faster towards the center.\n"
                     "Exponential: the density changes quickly just below the surface and levels off towards "
                     "the center.");
    def->init_fn = init_with(
        TpmsAdaptiveGradient::Linear,
        {{int(TpmsAdaptiveGradient::Linear), "linear", L("Linear")},
         {int(TpmsAdaptiveGradient::Quadratic), "quadratic", L("Quadratic")},
         {int(TpmsAdaptiveGradient::Exponential), "exponential", L("Exponential")}}
    );

    def = defs.add("gyroid_optimized", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Z-buckling bias optimization (experimental)");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 14;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Tightens the gyroid wave along the Z (vertical) axis at low infill density "
                     "to shorten the effective vertical column length and improve Z-axis compression "
                     "buckling resistance. Filament use is preserved. No effect at ~30% sparse infill "
                     "density and above. Only applies when Sparse infill pattern is set to Gyroid.");
    def->init_fn = init_with(false);

    def = defs.add("infill_shift_step", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Infill shift step");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 15;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This parameter adds a slight displacement to each layer of infill to create a cross texture.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 10;
    def->init_fn = init_with(0.4);

    def = defs.add("symmetric_infill_y_axis", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Symmetric infill Y axis");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 16;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If the model has two parts that are symmetric about the Y axis, and you want these parts to have symmetric textures, please click this option on one of the parts.");
    def->init_fn = init_with(false);

    def = defs.add("skin_infill_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Skin infill density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 17;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The portion of the model's outer surface within a certain depth range is called the skin. This parameter is used to adjust the density of this section. When two regions have the same sparse infill settings but different skin densities, this area will not be split into two separate regions. Default is as same as infill density.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(Percentage{25.});

    def = defs.add("skeleton_infill_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Skeleton infill density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 18;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The remaining part of the model contour after removing a certain depth from the surface is called the skeleton. This parameter is used to adjust the density of this section. When two regions have the same sparse infill settings but different skeleton densities, their skeleton areas will develop overlapping sections. Default is as same as infill density.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(Percentage{25.});

    def = defs.add("skin_infill_depth", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Skin infill depth");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 19;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The parameter sets the depth of skin.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(2.);

    def = defs.add("infill_lock_depth", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Infill lock depth");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 20;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The parameter sets the overlapping depth between the interior and skin.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(1.);

    def = defs.add("skin_infill_line_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Skin line width");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 21;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Adjust the line width of the selected skin paths.");
    def->ratio_over = "nozzle_diameter";
    def->units = {L("mm"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));

    def = defs.add("skeleton_infill_line_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Skeleton line width");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 22;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Adjust the line width of the selected skeleton paths.");
    def->ratio_over = "nozzle_diameter";
    def->units = {L("mm"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));

    def = defs.add("separated_infills", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Separated infills");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 23;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Centers the internal infill of each part on itself, as if it were sliced on its own, instead of on the "
                     "whole assembly. Parts that touch or overlap are treated as one body and share a center; separate parts "
                     "(or distinct 3D objects) each get their own.\n"
                     "Useful when an assembly groups several objects that should each keep a consistent, self-centered infill.\n"
                     "Adaptive Cubic and Support Cubic always center each part on itself, and Lightning infill is generated for "
                     "the whole object and is unaffected.");
    def->init_fn = init_with(false);

    def = defs.add("fill_multiline", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fill Multiline");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Using multiple lines for the infill pattern, if supported by infill pattern.");
    def->min = 1;
    def->max = 10;
    def->init_fn = init_with(1);

    def = defs.add("sparse_infill_smooth_factor", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Sparse infill smooth factor");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Controls how strongly sparse infill corners are rounded. 0% keeps the original sharp path, while 100% produces the largest possible curves between adjacent infill lines.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(Percentage{0.});

    def = defs.add("fill_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Fill pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Fill pattern for general low-density infill.");
    def->init_fn = init_with(
        InfillPattern::ipStars,
        {{int(InfillPattern::ipRectilinear), "rectilinear", L("Rectilinear")},
         {int(InfillPattern::ipAlignedRectilinear), "alignedrectilinear", L("Aligned Rectilinear")},
         {int(InfillPattern::ipGrid), "grid", L("Grid")},
         {int(InfillPattern::ipTriangles), "triangles", L("Triangles")},
         {int(InfillPattern::ipStars), "stars", L("Stars")},
         {int(InfillPattern::ipCubic), "cubic", L("Cubic")},
         {int(InfillPattern::ipLine), "line", L("Line")},
         {int(InfillPattern::ipConcentric), "concentric", L("Concentric")},
         {int(InfillPattern::ipHoneycomb), "honeycomb", L("Honeycomb")},
         {int(InfillPattern::ip3DHoneycomb), "3dhoneycomb", L("3D Honeycomb")},
         {int(InfillPattern::ipGyroid), "gyroid", L("Gyroid")},
         {int(InfillPattern::ipHilbertCurve), "hilbertcurve", L("Hilbert Curve")},
         {int(InfillPattern::ipArchimedeanChords), "archimedeanchords", L("Archimedean Chords")},
         {int(InfillPattern::ipOctagramSpiral), "octagramspiral", L("Octagram Spiral")},
         {int(InfillPattern::ipAdaptiveCubic), "adaptivecubic", L("Adaptive Cubic")},
         {int(InfillPattern::ipSupportCubic), "supportcubic", L("Support Cubic")},
         {int(InfillPattern::ipLightning), "lightning", L("Lightning")},
         {int(InfillPattern::ipZigZag), "zigzag", L("Zig Zag")},
         {int(InfillPattern::ip3DHoneycombClassic), "3dhoneycombclassic", L("3D Honeycomb (classic)")},
         {int(InfillPattern::ipQuarterCubic), "quartercubic", L("Quarter Cubic")},
         {int(InfillPattern::ipLateralLattice), "lateral-lattice", L("Lateral Lattice")},
         {int(InfillPattern::ipLateralHoneycomb), "lateral-honeycomb", L("Lateral Honeycomb")},
         {int(InfillPattern::ipCrossHatch), "crosshatch", L("Cross Hatch")},
         {int(InfillPattern::ipTpmsD), "tpmsd", L("TPMS-D")},
         {int(InfillPattern::ipTpmsFK), "tpmsfk", L("TPMS-FK")},
         {int(InfillPattern::ipCrossZag), "crosszag", L("Cross Zag")},
         {int(InfillPattern::ipLockedZag), "lockedzag", L("Locked Zag")}}
    );

    def = defs.add("first_layer_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_FirstLayerAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for first layer. Set zero "
                   "to disable acceleration control for first layer.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("first_layer_acceleration_over_raft", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("First object layer over raft interface");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_FirstLayerAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for first layer of object above raft interface. Set zero "
                   "to disable acceleration control for first layer of object above raft interface.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("first_layer_bed_temperature", typeid(int));
    def->location = Filament;
    def->label = L("Bed First layer");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("First layer bed temperature");
    def->tooltip = L("Heated build plate temperature for the first layer. Set this to zero to disable "
                   "bed temperature control commands in the output.");
    def->units = {L("°C")};
    def->max = 0;
    def->max = 300;
    def->init_fn = init_with(0);

    def = defs.add("first_layer_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for first layer. "
                   "You can use this to force fatter extrudates for better adhesion. If expressed "
                   "as percentage (for example 120%) it will be computed over nozzle_diameter. "
                   "If set to zero, it will use the default extrusion width.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage(Percentage{150.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("first_layer_height", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->compatibility_rule = CompatibilityRule::Min;
    def->label = L("First layer height");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_LayerHeight;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->order = 0;
    def->tooltip = L("When printing with very low layer heights, you might still want to print a thicker "
                   "bottom layer to improve adhesion and tolerance for non perfect build plates.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.2});
    def->ratio_over = "layer_height";

    // This parameter does not exist on the backend.
    def = defs.add("first_layer_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to all the print moves "
                   "of the first layer, regardless of their type. If expressed as a percentage "
                   "(for example: 40%) it will scale the default speeds.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->max_literal = 20;
    def->init_fn = init_with(FloatOrPercentage{30.});
    def->ratio_over = "none"; // It should be never resolved, as backend does not know about it.

    def = defs.add("first_layer_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to the infill print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the infill speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "infill_speed";

    def = defs.add("first_layer_solid_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer solid infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to the solid infill print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the solid infill speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "solid_infill_speed";

    def = defs.add("first_layer_perimeter_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer perimeter speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to the perimeter print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the perimeter speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "perimeter_speed";

    def = defs.add("first_layer_external_perimeter_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer perimeter speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to the external perimeter print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the external perimeter speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "external_perimeter_speed";

    def = defs.add("first_layer_support_material_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer support material speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to any support print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the support material speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "support_material_speed";

    def = defs.add("first_layer_top_solid_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer top solid infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to any top solid print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the top solid infill speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "top_solid_infill_speed";

    def = defs.add("first_layer_gap_fill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer gap fill speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to any gap fill print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the gap fill speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "gap_fill_speed";

    def = defs.add("first_layer_speed_over_raft", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Speed of object first layer over raft interface");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to all the print moves "
                   "of the first object layer above raft interface, regardless of their type. If expressed as a percentage "
                   "(for example: 40%) it will scale the default speeds.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{30.});
    def->ratio_over = "perimeter_speed";

    def = defs.add("first_layer_temperature", typeid(int));
    def->location = Filament;
    def->label = L("Nozzle First layer");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_NozzleTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("First layer nozzle temperature");
    def->tooltip = L("Nozzle temperature for the first layer. If you want to control temperature manually "
                     "during print, set this to zero to disable temperature control commands in the output G-code.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = max_temp;
    def->init_fn = init_with(200);

    def = defs.add("full_fan_speed_layer", typeid(int));
    def->location = Filament;
    def->label = L("Full fan speed at layer");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FirstLayers;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Fan speed will be ramped up linearly from zero at layer \"disable_fan_first_layers\" "
                   "to maximum at layer \"full_fan_speed_layer\". "
                   "\"full_fan_speed_layer\" will be ignored if lower than \"disable_fan_first_layers\", in which case "
                   "the fan will be running at maximum allowed speed at layer \"disable_fan_first_layers\" + 1.");
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(0);

    def = defs.add("fuzzy_skin", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy Skin");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Fuzzy skin type.");
    def->init_fn = init_with(
        FuzzySkinType::None,
        {{int(FuzzySkinType::None), "none", L("None")},
         {int(FuzzySkinType::External), "external", L("Outside walls")},
         {int(FuzzySkinType::All), "all", L("Outside walls and holes")},
         {int(FuzzySkinType::Hole), "hole", L("Holes")},
         {int(FuzzySkinType::AllWalls), "allwalls", L("All walls")}}
    );

    def = defs.add("fuzzy_skin_thickness", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy skin thickness");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 2;
    def->tooltip = L("The maximum distance that each skin point can be offset (both ways), "
                     "measured perpendicular to the perimeter wall.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.3);

    def = defs.add("fuzzy_skin_point_dist", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy skin point distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 1;
    def->tooltip = L("Perimeters will be split into multiple segments by inserting Fuzzy skin points. "
                     "Lowering the Fuzzy skin point distance will increase the number of randomly offset points on the perimeter wall.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.8);

    def = defs.add("fuzzy_skin_first_layer", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Apply fuzzy skin to first layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 3;
    def->tooltip = L("Whether to apply fuzzy skin on the first layer.");
    def->init_fn = init_with(false);

    def = defs.add("fuzzy_skin_mode", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy skin generator mode");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 4;
    def->tooltip = L("Fuzzy skin generation mode. Works only with Arachne! Displacement: Сlassic mode when the pattern is "
                     "formed by shifting the nozzle sideways from the original path. Extrusion: The mode when the pattern "
                     "formed by the amount of extruded plastic. This is the fast and straight algorithm without "
                     "unnecessary nozzle shake that gives a smooth pattern. But it is more useful for forming loose walls "
                     "in the entire they array. Combined: Joint mode [Displacement] + [Extrusion]. The appearance of the "
                     "walls is similar to [Displacement] Mode, but it leaves no pores between the perimeters. Attention! "
                     "The [Extrusion] and [Combined] modes works only the fuzzy_skin_thickness parameter not more than "
                     "the thickness of printed loop. At the same time, the width of the extrusion for a particular layer "
                     "should also not be below a certain level. It is usually equal 15-25%% of a layer height. Therefore, "
                     "the maximum fuzzy skin thickness with a perimeter width of 0.4 mm and a layer height of 0.2 mm will "
                     "be 0.4-(0.2*0.25)=±0.35mm! If you enter a higher parameter than this, the error Flow::spacing() "
                     "will displayed, and the model will not be sliced. You can choose this number until this error is "
                     "repeated.");
    def->init_fn = init_with(
        FuzzySkinMode::Displacement,
        {{int(FuzzySkinMode::Displacement), "displacement", L("Displacement")},
         {int(FuzzySkinMode::Extrusion), "extrusion", L("Extrusion")},
         {int(FuzzySkinMode::Combined), "combined", L("Combined")}}
    );

    def = defs.add("fuzzy_skin_noise_type", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy skin noise type");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 5;
    def->tooltip = L("Noise type to use for fuzzy skin generation: Classic: Classic uniform random noise. Perlin: Perlin "
                     "noise, which gives a more consistent texture. Billow: Similar to perlin noise, but clumpier. Ridged "
                     "Multifractal: Ridged noise with sharp, jagged features. Creates marble-like textures. Voronoi: "
                     "Divides the surface into voronoi cells, and displaces each one by a random amount. Creates a "
                     "patchwork texture. Ripple: Uniform ripple pattern that ripples left and right of the original path. "
                     "Repeating pattern, woven appearance.");
    def->init_fn = init_with(
        FuzzySkinNoiseType::Classic,
        {{int(FuzzySkinNoiseType::Classic), "classic", L("Classic")},
         {int(FuzzySkinNoiseType::Perlin), "perlin", L("Perlin")},
         {int(FuzzySkinNoiseType::Billow), "billow", L("Billow")},
         {int(FuzzySkinNoiseType::RidgedMulti), "ridgedmulti", L("Ridged Multifractal")},
         {int(FuzzySkinNoiseType::Voronoi), "voronoi", L("Voronoi")},
         {int(FuzzySkinNoiseType::Ripple), "ripple", L("Ripple")}}
    );

    def = defs.add("fuzzy_skin_scale", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy skin feature size");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 6;
    def->tooltip = L("The base size of the coherent noise features, in mm. Higher values will result in larger features.");
    def->units = {L("mm")};
    def->min = 0.1;
    def->max = 500;
    def->init_fn = init_with(1.0);

    def = defs.add("fuzzy_skin_octaves", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy Skin Noise Octaves");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 7;
    def->tooltip = L("The number of octaves of coherent noise to use. Higher values increase the detail of the noise, but "
                     "also increase computation time.");
    def->min = 1;
    def->max = 10;
    def->init_fn = init_with(4);

    def = defs.add("fuzzy_skin_persistence", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy skin noise persistence");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 8;
    def->tooltip = L("The decay rate for higher octaves of the coherent noise. Lower values will result in smoother noise.");
    def->min = 0.01;
    def->max = 1;
    def->init_fn = init_with(0.5);

    def = defs.add("fuzzy_skin_ripples_per_layer", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Number of ripples per layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 9;
    def->tooltip = L("Controls how many full cycles of ripples will be added per layer.");
    def->min = 1;
    def->init_fn = init_with(15);

    def = defs.add("fuzzy_skin_ripple_offset", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ripple offset");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 10;
    def->tooltip = L("Shifts the ripple phase forward along the print path by the specified percentage of a wavelength "
                     "each layer period. - 0% keeps every layer identical. - 50% shifts the pattern by half a wavelength, "
                     "effectively inverting the phase. - 100% shifts the pattern by a full wavelength, returning to the "
                     "original phase. The shift is applied once every number of layers set by Layers between ripple "
                     "offset, so layers within the same group are printed identically.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(Percentage{50.});

    def = defs.add("fuzzy_skin_layers_between_ripple_offset", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Layers between ripple offset");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 11;
    def->tooltip = L("Specifies how many consecutive layers share the same ripple phase before the offset is applied. For "
                     "example: - 1 = Layer 1 is printed with the base ripple pattern, then layer 2 is shifted by the "
                     "configured offset, then layer 3 returns to the base pattern, and so on. - 3 = Layers 1 to 3 are "
                     "printed with the base ripple pattern, then layers 4 to 6 are shifted by the configured offset, then "
                     "layers 7 to 9 return to the base pattern, etc.");
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("gap_fill_enabled", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Fill gaps");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enables filling of gaps between perimeters and between the inner most perimeters and infill.");
    def->init_fn = init_with(true);

    def = defs.add("gap_fill_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Gap fill");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for filling small gaps using short zigzag moves. Keep this reasonably low "
                   "to avoid too much shaking and resonance issues. Set zero to disable gaps filling.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(20.);

    def = defs.add("gcode_comments", typeid(bool));
    def->location = Print;
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Verbose G-code");
    def->tooltip = L("Enable this to get a commented G-code file, with each line explained by a descriptive text. "
                   "If you print from SD card, the additional weight of the file could make your firmware "
                   "slow down.");
    def->init_fn = init_with(false);

    def = defs.add("gcode_flavor", typeid(EnumWrapper));
    def->location = Printer;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->label = L("G-code flavor");
    def->tooltip = L("Some G/M-code commands, including temperature control and others, are not universal. "
                   "Set this option to your printer's firmware to get a compatible output. "
                   "The \"No extrusion\" flavor prevents PiggieSlicer from exporting any extrusion value at all.");
    def->init_fn = init_with(
        GCodeFlavor::gcfRepRapSprinter,
        {{int(GCodeFlavor::gcfRepRapSprinter), "reprap", L("RepRap/Sprinter")},
         {int(GCodeFlavor::gcfRepRapFirmware), "reprapfirmware", L("RepRapFirmware")},
         {int(GCodeFlavor::gcfRepetier), "repetier", L("Repetier")},
         {int(GCodeFlavor::gcfTeacup), "teacup", L("Teacup")},
         {int(GCodeFlavor::gcfMakerWare), "makerware", L("MakerWare (MakerBot)")},
         {int(GCodeFlavor::gcfMarlinLegacy), "marlin", L("Marlin (legacy)")},
         {int(GCodeFlavor::gcfMarlinFirmware), "marlin2", L("Marlin 2")},
         {int(GCodeFlavor::gcfPrusaFirmwareBuddy), "prusabuddy", L("Prusa Firmware (Buddy)")},
         {int(GCodeFlavor::gcfKlipper), "klipper", L("Klipper")},
         {int(GCodeFlavor::gcfSailfish), "sailfish", L("Sailfish (MakerBot)")},
         {int(GCodeFlavor::gcfMach3), "mach3", L("Mach3/LinuxCNC")},
         {int(GCodeFlavor::gcfMachinekit), "machinekit", L("Machinekit")},
         {int(GCodeFlavor::gcfSmoothie), "smoothie", L("Smoothie")},
         {int(GCodeFlavor::gcfNoExtrusion), "no-extrusion", L("No extrusion")}}
    );

    def = defs.add("gcode_label_objects", typeid(EnumWrapper));
    def->location = Print;
    def->label = L("Label objects");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Selects whether labels should be exported at object boundaries and in what format.\n"
                     "OctoPrint = comments to be consumed by OctoPrint CancelObject plugin.\n"
                     "Firmware = firmware specific G-code (it will be chosen based on firmware flavor and it can end up to be empty).\n\n"
                     "This settings is NOT compatible with Single Extruder Multi Material setup and Wipe into Object / Wipe into Infill.");
    def->init_fn = init_with(
        LabelObjectsStyle::Disabled,
        {{int(LabelObjectsStyle::Disabled), "disabled", L("Disabled")},
         {int(LabelObjectsStyle::Octoprint), "octoprint", L("OctoPrint comments")},
         {int(LabelObjectsStyle::Firmware), "firmware", L("Firmware-specific")}}
    );

    def = defs.add("gcode_substitutions", typeid(std::vector<std::string>));
    def->location = Print;
    def->label = L("G-code substitutions");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ScriptSubstitutions;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::substitutions;
    def->tooltip = L("Find / replace patterns in G-code lines and substitute them.");
    def->full_width = true;
    def->init_fn = init_with(std::vector<std::string>{});

    def = defs.add("high_current_on_filament_swap", typeid(bool));
    def->location = Printer;
    def->label = L("High extruder current on filament swap");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("It may be beneficial to increase the extruder motor current during the filament exchange"
                   " sequence to allow for rapid ramming feed rates and to overcome resistance when loading"
                   " a filament with an ugly shaped tip.");
    def->init_fn = init_with(false);

    // Ported from OrcaSlicer (enable_filament_ramming). Off for external multi-material units
    // whose firmware unloads the filament itself (e.g. with a filament cutter).
    def = defs.add("enable_filament_ramming", typeid(bool));
    def->location = Printer;
    def->label = L("Enable filament ramming");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Ram, cool and park the filament on the wipe tower before a filament change. "
                     "Turn this off when the multi-material unit unloads the filament itself.");
    def->init_fn = init_with(true);

    // Ported from OrcaSlicer's automatic flushing volume calculation.
    def = defs.add("wiping_volumes_by_colour", typeid(bool));
    def->location = Printer;
    def->label = L("Purging volumes from filament colours");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Calculate the purging volume of each filament change from the colours of the "
                     "two filaments, instead of using 'Purging volume'. "
                     "The filament purge multiplier still applies.");
    def->init_fn = init_with(false);

    // Ported from OrcaSlicer (purge_in_prime_tower, PrintConfig.cpp).
    def = defs.add("purge_in_prime_tower", typeid(bool));
    def->location = Printer;
    def->label = L("Purge in prime tower");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Purge remaining filament into prime tower. When off, only the prime volume "
                     "goes to the prime tower and the multi-material unit purges the rest itself.");
    def->init_fn = init_with(true);

    def = defs.add("enable_pressure_advance_during_ramming", typeid(bool));
    def->location = Printer;
    def->label = L("Enable pressure advance during ramming");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Check to enable pressure advance during ramming.");
    def->init_fn = init_with(false);

    def = defs.add("infill_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for infill. Set zero to disable "
                     "acceleration control for infill.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("solid_infill_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for solid infill. Set zero to use "
                     "the value for infill.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("top_solid_infill_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Top solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for top solid infill. Set zero to use "
                     "the value for solid infill.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("wipe_tower_acceleration", typeid(double));
    def->location = Print;
    def->label = L("Wipe tower");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_WipeTowerAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for wipe tower. Set zero to disable "
                     "acceleration control for the wipe tower.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("travel_acceleration", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("Travel");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelsAcceleration;
    def->category     = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for travel moves. Set zero to disable "
                     "acceleration control for travel.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def               = defs.add("travel_short_distance_acceleration", typeid(double));
    def->location     = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool };
    def->label        = L("Travel short distance acceleration");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelsAcceleration;
    def->category     = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 1;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip      = L(
        "Acceleration used for short travel moves. Short travel distance is determined by the retract_before_travel setting."
    );
    def->units = {L("mm/s²")};
    def->min      = 0;
    def->init_fn  = init_with(0.);

    def = defs.add("infill_every_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Combine infill every");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_InfillCombination;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This feature allows to combine infill and speed up your print by extruding thicker "
                   "infill layers while preserving thin perimeters, thus accuracy.");
    def->units = {L("layers")};
    def->full_label = L("Combine infill every n layers");
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("infill_anchor", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Length of the infill anchor");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_WallAnchoring;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "PiggieSlicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than infill_anchor_max is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to this parameter, but no longer than anchor_length_max. "
                     "Set this parameter to zero to disable anchoring perimeters connected to a single infill line.");
    def->units = {L("mm"), L("%")};;
    def->max_literal = 1000;
    def->choices = {
        { 0.,      L("0 (no open anchors)") },
        { 1.,      L("1 mm") },
        { 2.,      L("2 mm") },
        { 5.,      L("5 mm") },
        { 10.,     L("10 mm") },
        { 1000.,   L("1000 (unlimited)") }
    };
    def->init_fn = init_with(FloatOrPercentage(Percentage{600.}));
    def->ratio_over = "infill_extrusion_width";
    const ConfigItemDef* def_infill_anchor_min = def;

    def = defs.add("infill_anchor_max", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Maximum length of the infill anchor");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_WallAnchoring;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "PiggieSlicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than this parameter is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to infill_anchor, but no longer than this parameter. "
                     "Set this parameter to zero to disable anchoring.");
    def->units    = def_infill_anchor_min->units;
    def->max_literal = def_infill_anchor_min->max_literal;
    def->choices = {
        { 0.,      L("0 (not anchored)") },
        { 1.,      L("1 mm") },
        { 2.,      L("2 mm") },
        { 5.,      L("5 mm") },
        { 10.,     L("10 mm") },
        { 1000.,   L("1000 (unlimited)") }
    };
    def->init_fn = init_with(FloatOrPercentage{50.});
    def->ratio_over = "infill_extrusion_width";

    def = defs.add("infill_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Infill extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing infill.");
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("infill_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "You may want to use fatter extrudates to speed up the infill and make your parts stronger. "
                   "If expressed as percentage (for example 90%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("infill_first", typeid(bool));
    def->location = Print;
    def->label = L("Infill before perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This option will switch the print order of perimeters and infill, making the latter first.");
    def->init_fn = init_with(false);

    def = defs.add("infill_overlap", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Infill/perimeters overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Overlap;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("This setting applies an additional overlap between infill and perimeters for better bonding. "
                   "Theoretically this shouldn't be needed, but backlash might cause gaps. If expressed "
                   "as percentage (example: 15%) it is calculated over perimeter extrusion width.");
    def->units = {L("mm"), L("%")};;
    def->init_fn = init_with(FloatOrPercentage(Percentage{25.}));
    def->ratio_over = "perimeter_extrusion_width";

    def = defs.add("top_bottom_infill_wall_overlap", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Top/Bottom solid infill/wall overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Overlap;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Top solid infill area is enlarged slightly to overlap with wall for better bonding and to minimize "
                     "the appearance of pinholes where the top infill meets the walls. A value of 25-30% is a good "
                     "starting point, minimizing the appearance of pinholes. The percentage value is relative to the "
                     "inner wall line width.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(Percentage{25.});

    def = defs.add("min_width_top_surface", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("One wall threshold");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If a top surface has to be printed and it's partially covered by another layer, it won't be "
                     "considered at a top layer where its width is below this value. This can be useful to not let the "
                     "'one perimeter on top' trigger on surface that should be covered only by perimeters. This value can "
                     "be a mm or a % of the perimeter extrusion width. Warning: If enabled, artifacts can be created if "
                     "you have some thin features on the next layer, like letters. Set this setting to 0 to remove these "
                     "artifacts.");
    def->units = {L("mm"), L("%")};
    def->min = 0;
    // a percentage of the perimeter width: resolved in PerimeterGenerator
    def->init_fn = init_with(FloatOrPercentage(Percentage{300.}));

    def = defs.add("filter_out_gap_fill", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Filter out tiny gaps");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Don't print gap fill with a length is smaller than the threshold specified (in mm). This setting "
                     "applies to top, bottom and solid infill and, if using the classic perimeter generator, to wall gap "
                     "fill.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("infill_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for printing the internal fill. Set to zero for auto.");
    def->units = {L("mm/s")};
    def->aliases = { "print_feed_rate", "infill_feed_rate" };
    def->min = 0;
    def->init_fn = init_with(80.);

    /* TODO: Co s timhle?
    def = defs.add("inherits", typeid(std::string));
    def->label = L("Inherits profile");
    def->tooltip = L("Name of the profile, from which this profile inherits.");
    def->full_width = true;
    def->height = 5;
    def->init_fn = SET_DEFAULT( new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "inherits" values over the print and filament profiles.
    def = defs.add("inherits_cummulative", typeid(std::vector<std::string>));
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("interface_shells", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Interface shells");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Force the generation of solid shells between adjacent materials/volumes. "
                   "Useful for multi-extruder prints with translucent materials or manual soluble "
                   "support material.");
    def->init_fn = init_with(false);

    def = defs.add("mmu_segmented_region_max_width", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Maximum width of a segmented region");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 8;
    def->tooltip = L("Maximum width of a segmented region. Zero disables this feature.");
    def->units = {L("mm")};
    def->min = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->init_fn = init_with(0.);

    def = defs.add("mmu_segmented_region_interlocking_depth", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Interlocking depth of a segmented region");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 7;
    def->tooltip = L("Interlocking depth of a segmented region. It will be ignored if "
                       "\"mmu_segmented_region_max_width\" is zero or if \"mmu_segmented_region_interlocking_depth\""
                       "is bigger then \"mmu_segmented_region_max_width\". Zero disables this feature.");
    def->units = {L("mm")};
    def->min = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->init_fn = init_with(0.);

    def = defs.add("ironing", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Enable ironing");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable ironing of the top layers with the hot print head for smooth surface");
    def->init_fn = init_with(false);

    def           = defs.add("interlocking_beam", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Use beam interlocking");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 1;
    def->tooltip  = L("Generate interlocking beam structure at the locations where different filaments touch. This improves the adhesion between filaments, especially models printed in different materials.");
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->init_fn = init_with(false);

    def           = defs.add("interlocking_beam_width", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking beam width");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 3;
    def->tooltip  = L("The width of the interlocking structure beams.");
    def->units = {L("mm")};
    def->min      = 0.1f;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->init_fn = init_with(0.8);

    def           = defs.add("interlocking_orientation", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking direction");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 6;
    def->tooltip  = L("Orientation of interlocking beams.");
    def->units = {L("°")};
    def->min      = 0;
    def->max      = 360;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->init_fn = init_with(22.5);

    def           = defs.add("interlocking_beam_layer_count", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking beam layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = L("The height of the beams of the interlocking structure, measured in number of layers. Less layers is stronger, but more prone to defects.");
    def->min      = 1;
    def->init_fn = init_with(2);

    def           = defs.add("interlocking_depth", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking depth");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 5;
    def->tooltip  = L("The distance from the boundary between filaments to generate interlocking structure, measured in cells. Too few cells will result in poor adhesion.");
    def->min      = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->init_fn = init_with(2);

    def           = defs.add("interlocking_boundary_avoidance", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking boundary avoidance");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 4;
    def->tooltip  = L("The distance from the outside of a model where interlocking structures will not be generated, measured in cells.");
    def->min      = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->init_fn = init_with(2);

    def = defs.add("ironing_type", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ironing Type");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Ironing Type");
    def->init_fn = init_with(
        IroningType::TopSurfaces,
        {{int(IroningType::TopSurfaces), "top", L("All top surfaces")},
         {int(IroningType::TopmostOnly), "topmost", L("Topmost surface only")},
         {int(IroningType::AllSolid), "solid", L("All solid surfaces")}}
    );

    def = defs.add("ironing_flowrate", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Flow rate");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->order = 2;
    def->tooltip = L("Percent of a flow rate relative to object's normal layer height.");
    def->units = {L("%")};
    def->min = 0;
    def->init_fn = init_with(Percentage{15.});

    def = defs.add("ironing_spacing", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Spacing between ironing passes");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Distance between ironing lines");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.1);

    def = defs.add("ironing_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ironing speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Ironing");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(15.);

    def = defs.add("ironing_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ironing Pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("The pattern that will be used when ironing.");
    def->init_fn = init_with(
        InfillPattern::ipRectilinear,
        {{int(InfillPattern::ipRectilinear), "rectilinear", L("Rectilinear")},
         {int(InfillPattern::ipConcentric), "concentric", L("Concentric")}}
    );

    def = defs.add("ironing_inset", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ironing inset");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The distance to keep from the edges. A value of 0 sets this to half of the nozzle diameter.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(0.);

    def = defs.add("ironing_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ironing angle offset");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Angle of the ironing lines, relative to the top surface infill, or absolute with a fixed ironing angle.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 359;
    def->init_fn = init_with(0.);

    def = defs.add("ironing_angle_fixed", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fixed ironing angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Use a fixed absolute angle for ironing.");
    def->init_fn = init_with(false);

    def = defs.add("layer_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("After layer change G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This custom code is inserted at every layer change, right after the Z move "
                   "and before the extruder moves to the first layer point. Note that you can use "
                   "placeholder variables for all Slic3r settings as well as [layer_num] and [layer_z].");
    def->cli = "after-layer-gcode|layer-gcode";
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->init_fn = init_with("");

    def = defs.add("remaining_times", typeid(bool));
    def->location = Printer;
    def->label = L("Supports remaining times");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Emit M73 P[percent printed] R[remaining time in minutes] at 1 minute"
                     " intervals into the G-code to let the firmware show accurate remaining time."
                     " As of now only the Prusa i3 MK3 firmware recognizes M73."
                     " Also the i3 MK3 firmware supports M73 Qxx Sxx for the silent mode.");
    def->init_fn = init_with(false);

    def = defs.add("silent_mode", typeid(bool));
    def->location = Printer;
    def->label = L("Supports stealth mode");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("The firmware supports stealth mode");
    def->init_fn = init_with(true);

    def = defs.add("binary_gcode", typeid(bool));
    def->location = Printer;
    def->label = L("Supports binary G-code");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable, if the firmware supports binary G-code format (bgcode). "
                     "To generate .bgcode files, make sure you have binary G-code enabled in Configuration->Preferences->Other.");
    def->init_fn = init_with(false);

    def = defs.add("machine_limits_usage", typeid(EnumWrapper));
    def->location = Printer;
    def->label = L("How to apply limits");
    def->full_label = L("Purpose of Machine Limits");
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_General;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("How to apply the Machine Limits");
    def->init_fn = init_with(
        MachineLimitsUsage::TimeEstimateOnly,
        {{int(MachineLimitsUsage::EmitToGCode), "emit_to_gcode", L("Emit to G-code")},
         {int(MachineLimitsUsage::TimeEstimateOnly), "time_estimate_only",
          L("Use for time estimate")},
         {int(MachineLimitsUsage::Ignore), "ignore", L("Ignore")}}
    );

    {
        struct AxisDefault {
            std::string         name;
            std::vector<double> max_feedrate;
            std::vector<double> max_acceleration;
            std::vector<double> max_jerk;
        };
        std::vector<AxisDefault> axes {
            // name, max_feedrate,  max_acceleration, max_jerk
            { "x", { 500., 200. }, {  9000., 1000. }, { 10. , 10.  } },
            { "y", { 500., 200. }, {  9000., 1000. }, { 10. , 10.  } },
            { "z", {  12.,  12. }, {   500.,  200. }, {  0.2,  0.4 } },
            { "e", { 120., 120. }, { 10000., 5000. }, {  2.5,  2.5 } }
        };
        for (const AxisDefault &axis : axes) {
            std::string axis_upper = boost::to_upper_copy<std::string>(axis.name);
            // Add the machine feedrate limits for XYZE axes. (M203)
            def = defs.add("machine_max_feedrate_" + axis.name, typeid(std::vector<double>));
            def->location = Printer;
            def->label = (boost::format("Maximum feedrate %1%") % axis_upper).str();
            (void)L("Maximum feedrate X");
            (void)L("Maximum feedrate Y");
            (void)L("Maximum feedrate Z");
            (void)L("Maximum feedrate E");
            def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumFeedrates;
            def->category = ConfigItemDef::Category::Printer_MachineLimits;
            def->gui_type = ConfigItemDef::GUIType::textfields;
            def->tooltip  = (boost::format("Maximum feedrate of the %1% axis") % axis_upper).str();
            (void)L("Maximum feedrate of the X axis");
            (void)L("Maximum feedrate of the Y axis");
            (void)L("Maximum feedrate of the Z axis");
            (void)L("Maximum feedrate of the E axis");
            def->units = {L("mm/s")};
            def->min = 0;
            const std::vector<double> max_feedrate = axis.max_feedrate;
            def->init_fn = init_with(max_feedrate);

            // Add the machine acceleration limits for XYZE axes (M201)
            def = defs.add("machine_max_acceleration_" + axis.name, typeid(std::vector<double>));
            def->location = Printer;
            def->label = (boost::format("Maximum acceleration %1%") % axis_upper).str();
            (void)L("Maximum acceleration X");
            (void)L("Maximum acceleration Y");
            (void)L("Maximum acceleration Z");
            (void)L("Maximum acceleration E");
            def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumAccelerations;
            def->category = ConfigItemDef::Category::Printer_MachineLimits;
            def->gui_type = ConfigItemDef::GUIType::textfields;
            def->tooltip  = (boost::format("Maximum acceleration of the %1% axis") % axis_upper).str();
            (void)L("Maximum acceleration of the X axis");
            (void)L("Maximum acceleration of the Y axis");
            (void)L("Maximum acceleration of the Z axis");
            (void)L("Maximum acceleration of the E axis");
            def->units = {L("mm/s²")};
            def->min = 0;
            const std::vector<double> max_acceleration = axis.max_acceleration;
            def->init_fn = init_with(max_acceleration);

            // Add the machine jerk limits for XYZE axes (M205)
            def = defs.add("machine_max_jerk_" + axis.name, typeid(std::vector<double>));
            def->location = Printer;
            def->label = (boost::format("Maximum jerk %1%") % axis_upper).str();
            (void)L("Maximum jerk X");
            (void)L("Maximum jerk Y");
            (void)L("Maximum jerk Z");
            (void)L("Maximum jerk E");
            def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_JerkLimits;
            def->category = ConfigItemDef::Category::Printer_MachineLimits;
            def->gui_type = ConfigItemDef::GUIType::textfields;
            def->tooltip  = (boost::format("Maximum jerk of the %1% axis") % axis_upper).str();
            (void)L("Maximum jerk of the X axis");
            (void)L("Maximum jerk of the Y axis");
            (void)L("Maximum jerk of the Z axis");
            (void)L("Maximum jerk of the E axis");
            def->units = {L("mm/s")};
            def->min = 0;
            const std::vector<double> max_jerk = axis.max_jerk;
            def->init_fn = init_with(max_jerk);
        }
    }

    // M205 S... [mm/sec]
    def = defs.add("machine_min_extruding_rate", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Minimum feedrate when extruding");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MinimumFeedrates;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Minimum feedrate when extruding (M205 S)");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 0., 0. });

    // M205 J... [mm] machine junction deviation limits
    def = defs.add("machine_max_junction_deviation", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Maximum junction deviation");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_JunctionDeviation;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Maximum junction deviation (M205 J, only apply if JD > 0 for Marlin Firmware).");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 0., 0. });

    // M205 T... [mm/sec]
    def = defs.add("machine_min_travel_rate", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Minimum travel feedrate");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MinimumFeedrates;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Minimum travel feedrate (M205 T)");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 0., 0. });

    // M204 P... [mm/sec^2]
    def = defs.add("machine_max_acceleration_extruding", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Maximum acceleration when extruding");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumAccelerations;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Maximum acceleration when extruding");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 1500., 1250. });

    // M204 R... [mm/sec^2]
    def = defs.add("machine_max_acceleration_retracting", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Maximum acceleration when retracting");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumAccelerations;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Maximum acceleration when retracting.\n\n"
                     "Not used for RepRapFirmware, which does not support it.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 1500., 1250. });

    // M204 T... [mm/sec^2]
    def = defs.add("machine_max_acceleration_travel", typeid(std::vector<double>));
    def->location = Printer;
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumAccelerations;
    def->label = L("Maximum acceleration for travel moves");
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Maximum acceleration for travel moves.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 1500., 1250. });

    def = defs.add("max_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Max Fan speed");
    def->full_label = L("Max fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This setting represents the maximum speed of your fan.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(100);

    def = defs.add("max_layer_height", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Max");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_LayerHeight;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the highest printable layer height for this extruder, used to cap "
                   "the variable layer height and support layer height. Maximum recommended layer height "
                   "is 75% of the extrusion width to achieve reasonable inter-layer adhesion. "
                   "If set to 0, layer height is limited to 75% of the nozzle diameter.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("max_print_speed", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Max print speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_VolumetricSpeed;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("When setting other speed settings to 0 Slic3r will autocalculate the optimal speed "
                   "in order to keep constant extruder pressure. This experimental setting is used "
                   "to set the highest print speed you want to allow.");
    def->units = {L("mm/s")};
    def->min = 1;
    def->init_fn = init_with(80.);

    def = defs.add("max_volumetric_speed", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Max volumetric speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_VolumetricSpeed;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This experimental setting is used to set the maximum volumetric speed your "
                   "extruder supports.");
    def->units = {L("mm³/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("max_volumetric_extrusion_rate_slope_positive", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("Max volumetric slope positive");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_PressureEqualizer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate "
                       "for a transition from lower speed to higher speed. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45 mm extrusion width, 0.2 mm extrusion height, feedrate 20 mm/s) "
                   "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    def->units = {L("mm³/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("max_volumetric_extrusion_rate_slope_negative", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("Max volumetric slope negative");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_PressureEqualizer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate "
                       "for a transition from higher speed to lower speed. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 5.4 mm³/s (0.45 mm extrusion width, 0.2 mm extrusion height, feedrate 60 mm/s) "
                   "to 1.8 mm³/s (feedrate 20 mm/s) will take at least 2 seconds.");
    def->units = {L("mm³/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("max_volumetric_extrusion_rate_slope_segment_length", typeid(double));
    def->location = Print;
    def->label = L("Smoothing segment length");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_PressureEqualizer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("A lower value results in smoother extrusion rate transitions. However, this results in a "
                     "significantly larger G-code file and more instructions for the printer to process. Default value of "
                     "3 works well for most cases. If your printer is stuttering, increase this value to reduce the "
                     "number of adjustments made. Allowed values: 0.5-5");
    def->units = {L("mm")};
    def->min = 0.5;
    def->max = 5;
    def->init_fn = init_with(3.);

    def = defs.add("extrusion_rate_smoothing_external_perimeter_only", typeid(bool));
    def->location = Print;
    def->label = L("Apply only on external features");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_PressureEqualizer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Applies extrusion rate smoothing only on external perimeters and overhangs. This can help reduce "
                     "artefacts due to sharp speed transitions on externally visible overhangs without impacting the "
                     "print speed of features that will not be visible to the user.");
    def->init_fn = init_with(false);

    def = defs.add("min_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Min fan speed");
    def->full_label = L("Min fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This setting represents the minimum PWM your fan needs to work.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(35);

    def = defs.add("min_layer_height", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Min");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_LayerHeight;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the lowest printable layer height for this extruder and limits "
                   "the resolution for variable layer height. Typical values are between 0.05 mm and 0.1 mm.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.07);

    def = defs.add("min_print_speed", typeid(double));
    def->location = Filament;
    def->label = L("Min print speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingThresholds;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Slic3r will not scale speed down below this speed.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(10.);

    def = defs.add("min_skirt_length", typeid(double));
    def->location = Print;
    def->label = L("Minimal filament extrusion length");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Generate no less than the number of skirt loops required to consume "
                   "the specified amount of filament on the bottom layer. For multi-extruder machines, "
                   "this minimum applies to each extruder.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("notes", typeid(std::string));
    def->location = Print;
    def->label = L("Configuration notes");
    def->option_group = ConfigItemDef::OptionGroup::Print_Notes_Notes;
    def->category = ConfigItemDef::Category::Print_Notes;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("You can put here your personal notes. This text will be added to the G-code "
                   "header comments.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->init_fn = init_with("");

    def = defs.add("only_retract_when_crossing_perimeters", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("Only retract when crossing perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Disables retraction when the travel path does not exceed the upper layer's perimeters "
                   "(and thus any ooze will be probably invisible).");
    def->init_fn = init_with(false);

    def = defs.add("ooze_prevention", typeid(bool));
    def->location = Print;
    def->label = L("Enable");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_OozePrevention;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    // TRN PrintSettings: Enable ooze prevention
    def->tooltip = L("This option will drop the temperature of the inactive extruders to prevent oozing.");
    def->init_fn = init_with(false);

    def = defs.add("overhangs", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Detect bridging perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Experimental option to adjust flow for overhangs (bridge flow will be used), "
                   "to apply bridge speed to them and enable fan.");
    def->init_fn = init_with(true);

    def = defs.add("overhang_reverse", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Reverse on even");
    def->full_label = L("Reverse on even layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Extrude perimeters that have a part over an overhang in the reverse direction on even layers. This "
                     "alternating pattern can drastically improve steep overhangs. This setting can also help reduce part "
                     "warping due to the reduction of stresses in the part walls.");
    def->init_fn = init_with(false);

    def = defs.add("overhang_reverse_internal_only", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Reverse only internal perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Apply the reverse perimeters logic only on internal perimeters. This setting greatly reduces part "
                     "stresses as they are now distributed in alternating directions. This should reduce part warping "
                     "while also maintaining external wall quality. This feature can be very useful for warp prone "
                     "material, like ABS/ASA, and also for elastic filaments, like TPU and Silk PLA. It can also help "
                     "reduce warping on floating regions over supports. For this setting to be the most effective, it is "
                     "recommended to set the Reverse Threshold to 0 so that all internal walls print in alternating "
                     "directions on even layers irrespective of their overhang degree.");
    def->init_fn = init_with(false);

    def = defs.add("overhang_reverse_threshold", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Reverse threshold");
    def->full_label = L("Overhang reversal threshold");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Number of mm the overhang need to be for the reversal to be considered useful. Can be a % of the "
                     "perimeter width. Value 0 enables reversal on every even layers regardless. When Detect overhang "
                     "wall is not enabled, this option is ignored and reversal happens on every even layers regardless.");
    def->units = {L("mm"), L("%")};
    def->min = 0;
    // a percentage of the perimeter's width: resolved in PerimeterGenerator
    def->init_fn = init_with(FloatOrPercentage(Percentage{50.}));

    def = defs.add("alternate_extra_wall", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Alternate extra wall");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This setting adds an extra wall to every other layer. This way the infill gets wedged vertically "
                     "between the walls, resulting in stronger prints. When this option is enabled, the ensure vertical "
                     "shell thickness option needs to be disabled. Using lightning infill together with this option is "
                     "not recommended as there is limited infill to anchor the extra perimeters to.");
    def->init_fn = init_with(false);

    def = defs.add("unsupported_wall_last", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Print unsupported walls last");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 8;
    def->tooltip = L("Wall loops that lie entirely in mid air are printed once something can hold them: they are extruded "
                     "after the other walls of their island, innermost first, whatever the wall order is. A loop that "
                     "only the bridges of this layer can anchor waits until those bridges are printed, while a loop "
                     "running alongside a supported wall keeps its place before the infill, which needs it as an anchor.");
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->init_fn = init_with(false);

    def = defs.add("parking_pos_retraction", typeid(double));
    def->location = Printer;
    def->label = L("Filament parking position");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Distance of the extruder tip from the position where the filament is parked "
                      "when unloaded. This should match the value in printer firmware.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(92.);

    def = defs.add("extra_loading_move", typeid(double));
    def->location = Printer;
    def->label = L("Extra loading distance");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("When set to zero, the distance the filament is moved from parking position during load "
                      "is exactly the same as it was moved back during unload. When positive, it is loaded further, "
                      " if negative, the loading move is shorter than unloading.");
    def->units = {L("mm")};
    def->init_fn = init_with(-2.);

    def = defs.add("multimaterial_purging", typeid(double));
    def->location = Printer;
    def->label = L("Purging volume");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Determines purging volume on the wipe tower. This can be modified in Filament Settings "
                     "('filament_purge_multiplier') or overridden using project-specific settings.");
    def->units = {L("mm³")};
    def->init_fn = init_with(140.);

    def = defs.add("perimeter_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for perimeters. "
                     "Set zero to disable acceleration control for perimeters.");
    def->units = {L("mm/s²")};
    def->init_fn = init_with(0.);

    def = defs.add("external_perimeter_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("External perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for external perimeters. "
                     "Set zero to use the value for perimeters.");
    def->units = {L("mm/s²")};
    def->init_fn = init_with(0.);

    def = defs.add("perimeter_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Perimeter extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing perimeters and brim. First extruder is 1.");
    def->aliases = { "perimeters_extruder" };
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("perimeter_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for perimeters. "
                   "You may want to use thinner extrudates to get more accurate surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 200%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->aliases = { "perimeters_extrusion_width" };
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("perimeter_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for perimeters (contours, aka vertical shells). Set to zero for auto.");
    def->units = {L("mm/s")};
    def->aliases = { "perimeter_feed_rate" };
    def->min = 0;
    def->init_fn = init_with(60.);

    def = defs.add("perimeters", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Perimeters;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This option sets the minimal number of perimeters to generate for each layer. "
                   "Note that Slic3r may increase this number automatically when it detects "
                   "sloping surfaces which benefit from a higher number of perimeters "
                   "if the Extra Perimeters option is enabled.");
    def->aliases = { "perimeter_offsets" };
    def->min = 0;
    def->max = 10000;
    def->init_fn = init_with(3);

    def = defs.add("post_process", typeid(std::vector<std::string>));
    def->location = Print;
    def->label = L("Post-processing scripts");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ScriptSubstitutions;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("If you want to process the output G-code through custom scripts, "
                   "just list their absolute paths here. Separate multiple scripts with a semicolon. "
                   "Scripts will be passed the absolute path to the G-code file as the first argument, "
                   "and they can access the Slic3r config settings by reading environment variables.");
    def->multiline = true;
    def->full_width = true;
    def->height = 6;
    def->init_fn = init_with(std::vector<std::string>{});

    def               = defs.add("pressure_advance", typeid(EnumWrapper));
    def->location     = Filament;
    def->label        = L("Pressure advance");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_PressureAdvance;
    def->category     = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order        = 0;
    def->gui_type     = ConfigItemDef::GUIType::combobox;
    def->tooltip      = L(
        "Controls how the pressure advance value is emitted into g-code for this filament. "
             "'Disabled' does not emit any pressure advance command, leaving the printer to use the pressure advance value set in the filament's start g-code (or the firmware default). "
             "'Enabled' emits the configured pressure advance value. "
             "'Automatic Calibration' emits the configured pressure advance value and then triggers the firmware's automatic pressure advance calibration."
    );
    def->init_fn = init_with(
        PressureAdvance::Disabled,
        {{int(PressureAdvance::Disabled), "disabled", L("Disabled")},
         {int(PressureAdvance::Enabled), "enabled", L("Enabled")},
         {int(PressureAdvance::AutomaticCalibration),
          "automatic_calibration",
          L("Automatic Calibration")}}
    );

    def               = defs.add("pressure_advance_value", typeid(double));
    def->location     = Filament;
    def->label        = L("Pressure advance");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_PressureAdvance;
    def->category     = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 1;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip      = L(
        "Pressure Advance (also called Linear Advance in some firmware) compensates "
             "for nozzle pressure to achieve sharper corners. Proper value helps reduce "
             "bulging on deceleration and under-extrusion on acceleration."
    );

    def->min = 0.;
    def->init_fn = init_with(0.);

    def               = defs.add("adaptive_pressure_advance", typeid(bool));
    def->location     = Filament;
    def->label = L("Enable adaptive pressure advance (beta)");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_PressureAdvance;
    def->category     = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order        = 2;
    def->gui_type     = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("With increasing print speeds (and hence increasing volumetric flow through the nozzle) and "
                     "increasing accelerations, it has been observed that the effective PA value typically decreases. "
                     "This means that a single PA value is not always 100% optimal for all features and a compromise "
                     "value is usually used that does not cause too much bulging on features with lower flow speed and "
                     "accelerations while also not causing gaps on faster features. This feature aims to address this "
                     "limitation by modeling the response of your printer's extrusion system depending on the volumetric "
                     "flow speed and acceleration it is printing at. Internally, it generates a fitted model that can "
                     "extrapolate the needed pressure advance for any given volumetric flow speed and acceleration, which "
                     "is then emitted to the printer depending on the current print conditions. When enabled, the "
                     "pressure advance value above is overridden. However, a reasonable default value above is strongly "
                     "recommended to act as a fallback and for when tool changing.");
    def->init_fn = init_with(false);

    def               = defs.add("adaptive_pressure_advance_overhangs", typeid(bool));
    def->location     = Filament;
    def->label = L("Enable adaptive pressure advance within features (beta)");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_PressureAdvance;
    def->category     = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order        = 3;
    def->gui_type     = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable adaptive PA whenever there are flow changes in a feature, such as line width changes in a "
                     "corner or overhangs. Not compatible with Prusa printers as they pause to process PA changes, which "
                     "causes delays and defects. This is an experimental option, as if the PA profile is not set "
                     "accurately, it will cause uniformity issues.");
    def->init_fn = init_with(false);

    def               = defs.add("adaptive_pressure_advance_bridges", typeid(double));
    def->location     = Filament;
    def->label = L("Static pressure advance for bridges");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_PressureAdvance;
    def->category     = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order        = 4;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Static pressure advance value for bridges. Set to 0 to apply the same pressure advance as "
                     "equivalent walls (using adaptive settings if enabled). A lower PA value when printing bridges helps "
                     "reduce the appearance of slight under-extrusion immediately after bridges. This is caused by the "
                     "pressure drop in the nozzle when printing in the air and a lower PA helps counteract this.");
    def->min = 0.;
    def->max = 2;
    def->init_fn = init_with(0.);

    def               = defs.add("adaptive_pressure_advance_model", typeid(std::string));
    def->location     = Filament;
    def->label = L("Adaptive pressure advance measurements (beta)");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_PressureAdvance;
    def->category     = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order        = 5;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Add sets of pressure advance (PA) values, the volumetric flow speeds and accelerations they were "
                     "measured at, separated by a comma. One set of values per line. For example 0.04,3.96,3000 "
                     "0.033,3.96,10000 0.029,7.91,3000 0.026,7.91,10000 How to calibrate: 1. Run the pressure advance "
                     "test for at least 3 speeds per acceleration value. It is recommended that the test is run for at "
                     "least the speed of the external perimeters, the speed of the internal perimeters and the fastest "
                     "feature print speed in your profile (usually its the sparse or solid infill). Then run them for the "
                     "same speeds for the slowest and fastest print accelerations, and no faster than the recommended "
                     "maximum acceleration as given by the Klipper input shaper 2. Take note of the optimal PA value for "
                     "each volumetric flow speed and acceleration. You can find the flow number by selecting flow from "
                     "the color scheme drop down and move the horizontal slider over the PA pattern lines. The number "
                     "should be visible at the bottom of the page. The ideal PA value should be decreasing the higher the "
                     "volumetric flow is. If it is not, confirm that your extruder is functioning correctly. The slower "
                     "and with less acceleration you print, the larger the range of acceptable PA values. If no "
                     "difference is visible, use the PA value from the faster test 3. Enter the triplets of PA values, "
                     "Flow and Accelerations in the text box here and save your filament profile.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->init_fn = init_with(std::string{"0,0,0\n0,0,0"});

    def = defs.add("printer_vendor", typeid(std::string));
    def->location = Printer;
    def->label = L("Printer vendor");
    def->category = ConfigItemDef::Category::Hidden;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Name of the printer vendor.");
    def->cli = ConfigItemDef::nocli;
    def->init_fn = init_with("");


    /* TODO: What about this?
    def = defs.add("print_settings_id", typeid(std::string));
    def->init_fn = SET_DEFAULT(""));
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("printer_settings_id", typeid(std::string));
    def->init_fn = SET_DEFAULT(""));
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("physical_printer_settings_id", typeid(std::string));
    def->init_fn = SET_DEFAULT(""));
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("raft_contact_distance", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Raft contact Z distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The vertical distance between object and raft. Ignored for soluble interface.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.1);

    def = defs.add("raft_expansion", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Raft expansion");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Expansion of the raft in XY plane for better stability.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(1.5);

    def                             = defs.add("raft_first_layer_density", typeid(Percentage));
    def->location                   = Print;
    def->compatibility_rule         = CompatibilityRule::Average;
    def->overrides_in               = Locations{Tool, Object};
    def->label                      = L("First layer density");
    def->option_group               = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category                   = ConfigItemDef::Category::Print_BedAdhesion;
    def->order                      = 3;
    def->gui_type                   = ConfigItemDef::GUIType::textfield;
    def->tooltip                    = L("Density of the first raft layer.");
    def->units                   = {L("%")};
    def->min                        = 10;
    def->max                        = 100;
    def->init_fn                    = init_with(Percentage{90.});

    def                             = defs.add("raft_first_layer_expansion", typeid(double));
    def->location                   = Print;
    def->label                      = L("Raft first layer expansion");
    def->compatibility_rule         = CompatibilityRule::Max;
    def->overrides_in               = Locations{Tool, Object};
    def->option_group               = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category                   = ConfigItemDef::Category::Print_BedAdhesion;
    def->order                      = 4;
    def->gui_type                   = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("Expansion of the raft first layer to improve adhesion to print bed.");
    def->units = {L("mm")};
    def->min      = 0;
    def->init_fn  = init_with(3.);

    def = defs.add("raft_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Raft layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The object will be raised by this number of layers, and support material "
                   "will be generated under it.");
    def->units = {L("layers")};
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("resolution", typeid(double));
    def->location = Print;
    def->label = L("Slice resolution");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Minimum detail resolution, used to simplify the input file for speeding up "
                   "the slicing job and reducing memory usage. High-resolution models often carry "
                   "more detail than printers can render. Set to zero to disable any simplification "
                   "and use full resolution from input.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("gcode_resolution", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("G-code resolution");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum deviation of exported G-code paths from their full resolution counterparts. "
                     "Very high resolution G-code requires huge amount of RAM to slice and preview, "
                     "also a 3D printer may stutter not being able to process a high resolution G-code in a timely manner. "
                     "On the other hand, a low resolution G-code will produce a low poly effect and because "
                     "the G-code reduction is performed at each layer independently, visible artifacts may be produced.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.0125);

    def = defs.add("retract_before_travel", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Minimum travel after retraction");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Retraction is not triggered when travel moves are shorter than this length.");
    def->units = {L("mm")};
    def->init_fn = init_with(2.);

    def = defs.add("retract_before_wipe", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Retract amount before wipe");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("With bowden extruders, it may be wise to do some amount of quick retract "
                   "before doing the wipe movement.");
    def->units = {L("%")};
    def->init_fn = init_with(Percentage{0.});

    def = defs.add("retract_layer_change", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Retract on layer change");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This flag enforces a retraction whenever a Z move is done.");
    def->init_fn = init_with(false);

    def = defs.add("retract_length", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Retraction length");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Retraction Length");
    def->tooltip = L("When retraction is triggered, filament is pulled back by the specified amount "
                   "(the length is measured on raw filament, before it enters the extruder).");
    def->units = {L("mm")};
    def->init_fn = init_with(2.);

    def = defs.add("retract_length_toolchange", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Length");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_IdleToolRetraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Retraction Length (Toolchange)");
    def->tooltip = L("When retraction is triggered before changing tool, filament is pulled back "
                   "by the specified amount (the length is measured on raw filament, before it enters "
                   "the extruder).");
    def->units = {L("mm")};
    def->init_fn = init_with(10.);

    def = defs.add("travel_slope", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Ramping slope angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Slope of the ramp in the initial phase of the travel.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 90;
    def->init_fn = init_with(0.);

    def = defs.add("travel_ramping_lift", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Use ramping lift");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Generates a ramping lift instead of lifting the extruder directly upwards. "
                     "The travel is split into two phases: the ramp and the standard horizontal travel. "
                     "This option helps reduce stringing.");
    def->init_fn = init_with(false);

    def = defs.add("travel_max_lift", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Maximum ramping lift");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum lift height of the ramping lift. It may not be reached if the next position "
                     "is close to the old one.");
    def->units = {L("mm")};
    def->min = 0;
    def->max_literal = 1000;
    def->init_fn = init_with(0.);

    def = defs.add("travel_lift_before_obstacle", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Steeper ramp before obstacles");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, PiggieSlicer detects obstacles along the travel path and makes the slope steeper "
                     "in case an obstacle might be hit during the initial phase of the travel.");
    def->init_fn = init_with(false);

    def = defs.add("retract_lift", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Lift height");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Lift height applied before travel.");
    def->units = {L("mm")};
    def->min = 0;
    def->max_literal = 1000;
    def->init_fn = init_with(0.);

    def = defs.add("retract_lift_above", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Only lift Above Z");
    // def->row_group = L("Only lift"); Temporary removed, row_group are not supported in PrintTool
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Only lift Z above");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place above the specified "
                   "absolute Z. You can tune this setting for skipping lift on the first layers.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("retract_lift_below", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Only lift Below Z");
    // def->row_group = L("Only lift"); Temporary removed, row_group are not supported in PrintTool
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Only lift Z below");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place below "
                   "the specified absolute Z. You can tune this setting for limiting lift "
                   "to the first layers.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("retract_lift_enforce", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Lift Z on surfaces");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Where retractions lift Z: everywhere, only after top surfaces, only on the first layer, or both. "
                     "Within the 'Only lift Z' limits.");
    def->init_fn = init_with(
        RetractLiftEnforce::AllSurfaces,
        {
            {int(RetractLiftEnforce::AllSurfaces),  "all_surfaces",   L("All surfaces")},
            {int(RetractLiftEnforce::TopOnly),      "top_only",       L("Top only")},
            {int(RetractLiftEnforce::BottomOnly),   "bottom_only",    L("Bottom only")},
            {int(RetractLiftEnforce::TopAndBottom), "top_and_bottom", L("Top and bottom")},
        }
    );

    def = defs.add("retract_restart_extra", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Deretraction extra length");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("When the retraction is compensated after the travel move, the extruder will push "
                   "this additional amount of filament. This setting is rarely needed.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("retract_restart_extra_toolchange", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Extra length on restart");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_IdleToolRetraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("When the retraction is compensated after changing tool, the extruder will push "
                   "this additional amount of filament.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("retract_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Retraction Speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Retraction Speed");
    def->tooltip = L("The speed for retractions (it only applies to the extruder motor).");
    def->units = {L("mm/s")};
    def->init_fn = init_with(40.);

    def = defs.add("deretract_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Deretraction Speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Deretraction Speed");
    def->tooltip = L("The speed for loading of a filament into extruder after retraction "
                   "(it only applies to the extruder motor). If left to zero, the retraction speed is used.");
    def->units = {L("mm/s")};
    def->init_fn = init_with(0.);

    def = defs.add("seam_gap_distance", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Filament };
    def->label = L("Seam gap distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("The distance between the endpoints of a closed loop perimeter. "
                   "Positive values will shorten and interrupt the loop slightly to reduce the seam. "
                   "Negative values will extend the loop, causing the endpoints to overlap slightly. "
                   "When percents are used, the distance is derived from the nozzle diameter. "
                   "Set to zero to disable this feature.");
    def->units = {L("mm"), L("%")};;
    def->init_fn = init_with(FloatOrPercentage(Percentage{15.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("seam_position", typeid(EnumWrapper));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Seam position");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Position of perimeters starting points.");
    def->init_fn = init_with(
        SeamPosition::spAligned,
        {{int(SeamPosition::spRandom), "random", L("Random")},
         {int(SeamPosition::spNearest), "nearest", L("Nearest")},
         {int(SeamPosition::spAligned), "aligned", L("Aligned")},
         {int(SeamPosition::spRear), "rear", L("Rear")}}
    );

    def = defs.add("staggered_inner_seams", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Staggered inner seams");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    // TRN PrintSettings: "Staggered inner seams"
    def->tooltip = L("This option causes the inner seams to be shifted backwards based on their depth, forming a zigzag pattern.");
    def->init_fn = init_with(false);

    def = defs.add("scarf_seam_placement", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint placement");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Where to place scarf joint seam.");
    def->init_fn = init_with(
        ScarfSeamPlacement::nowhere,
        {// TRN: Drop-down option for 'Scarf joint placement' parameter.
         {int(ScarfSeamPlacement::nowhere), "nowhere", L("Nowhere")},
         // TRN: Drop-down option for 'Scarf joint placement' parameter.
         {int(ScarfSeamPlacement::countours), "contours", L("Contours")},
         {int(ScarfSeamPlacement::everywhere), "everywhere", L("Everywhere")}
        }
    );

    def = defs.add("scarf_seam_only_on_smooth", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint only on smooth perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Only use the scarf joint when the perimeter is smooth.");
    def->init_fn = init_with(true);

    def = defs.add("scarf_seam_start_height", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf start height");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Start height of the scarf joint specified as fraction of the current layer height.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(Percentage{0.});

    def = defs.add("scarf_seam_entire_loop", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint around entire perimeter");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Extend the scarf around entire length of the perimeter.");
    def->init_fn = init_with(false);

    def = defs.add("scarf_seam_length", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint length");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Length of the scarf joint.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(20.);

    def = defs.add("scarf_seam_max_segment_length", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Max scarf joint segment length");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum length of any scarf joint segment.");
    def->units = {L("mm")};
    def->min = 0.15f;
    def->init_fn = init_with(1.0);

    def = defs.add("scarf_seam_on_inner_perimeters", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint on inner perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Use scarf joint on inner perimeters.");
    def->init_fn = init_with(false);

    def = defs.add("scarf_angle_threshold", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Conditional angle threshold");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This option sets the threshold angle for applying a conditional scarf joint seam. If the maximum "
                     "angle within the perimeter loop exceeds this value (indicating the absence of sharp corners), a "
                     "scarf joint seam will be used. The default value is 155°.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 180;
    def->init_fn = init_with(155);

    def = defs.add("scarf_overhang_threshold", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Conditional overhang threshold");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 11;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This option determines the overhang threshold for the application of scarf joint seams. If the "
                     "unsupported portion of the perimeter is less than this threshold, scarf joint seams will be "
                     "applied. The default threshold is set at 40% of the external wall's width. Due to performance "
                     "considerations, the degree of overhang is estimated.");
    def->units = {L("%")};
    def->min = 0;
    def->init_fn = init_with(Percentage{40.});

    def = defs.add("seam_slope_steps", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf steps");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 12;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Minimum number of segments of each scarf.");
    def->min = 1;
    def->full_label = L("Scarf joint steps");
    def->init_fn = init_with(10);

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 1. Defaults keep 3.0's behaviour
    // (converted Orca profiles carry Orca's own values).
    def = defs.add("wipe_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->label = L("Wipe speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 20;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("The wipe speed is determined by the speed setting specified in this configuration. If the value is "
                     "expressed as a percentage (e.g. 80%), it will be calculated based on the travel speed setting "
                     "above. The default value for this parameter is 80%.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{80.}));
    def->ratio_over = "travel_speed";

    def = defs.add("role_based_wipe_speed", typeid(bool));
    def->location = Print;
    def->label = L("Role based wipe speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 21;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("The wipe speed is determined by the speed of the current extrusion role. e.g. if a wipe "
                     "action is executed immediately following an outer wall extrusion, the speed of the "
                     "outer wall extrusion will be utilized for the wipe action.");
    def->init_fn = init_with(false);

    def = defs.add("skirt_speed", typeid(double));
    def->location = Print;
    def->label = L("Skirt speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed of skirt, in mm/s. Zero means use default layer extrusion speed.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(50.);

    def = defs.add("initial_layer_travel_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->label = L("First layer travel speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 20;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Travel speed of the first layer.");
    def->units = {L("mm/s"), L("%")};
    def->min = 1;
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "travel_speed";

    def = defs.add("change_extrusion_role_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("Change extrusion role G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 20;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This G-code is inserted when the extrusion role is changed.");
    def->multiline = true;
    def->full_width = true;
    def->init_fn = init_with(std::string{});

    def = defs.add("process_change_extrusion_role_gcode", typeid(std::string));
    def->location = Print;
    def->label = L("Change extrusion role G-code (process)");
    def->category = ConfigItemDef::Category::Print_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Print_CustomGCode;
    def->order = 20;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This G-code is inserted when the extrusion role is changed. It runs after the machine and filament "
                     "extrusion role G-code.");
    def->multiline = true;
    def->full_width = true;
    def->init_fn = init_with(std::string{});

    def = defs.add("filament_change_extrusion_role_gcode", typeid(std::string));
    def->location = Filament;
    def->label = L("Change extrusion role G-code (filament)");
    def->category = ConfigItemDef::Category::Filament_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Filament_CustomGCode;
    def->order = 20;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This G-code is inserted when the extrusion role is changed for the active filament.");
    def->multiline = true;
    def->full_width = true;
    def->init_fn = init_with(std::string{});

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 2, flow ratios (1 = 3.0's flow).
    def = defs.add("set_other_flow_ratios", typeid(bool));
    def->location = Print;
    def->label = L("Set other flow ratios");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 30;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Change flow ratios for other extrusion path types.");
    def->init_fn = init_with(false);

    def = defs.add("print_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 31;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The material may have volumetric change after switching between molten and crystalline states. This "
                     "setting changes all extrusion flow of this filament in G-code proportionally. The recommended value "
                     "range is between 0.95 and 1.05. You may be able to tune this value to get a nice flat surface if "
                     "there is slight overflow or underflow. The final object flow ratio is this value multiplied by the "
                     "filament flow ratio.");
    def->min = 0.01;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("top_solid_infill_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Top surface flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 32;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for top solid infill. You can decrease it slightly to "
                     "have smooth surface finish. The actual top surface flow used is calculated by multiplying this "
                     "value with the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("outer_wall_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Outer wall flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 33;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for outer walls. The actual outer wall flow used is "
                     "calculated by multiplying this value by the filament flow ratio, and if set, the object's flow "
                     "ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("inner_wall_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Inner wall flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 34;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for inner walls. The actual inner wall flow used is "
                     "calculated by multiplying this value by the filament flow ratio, and if set, the object's flow "
                     "ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("overhang_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Overhang flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 35;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for overhangs. The actual overhang flow used is "
                     "calculated by multiplying this value by the filament flow ratio, and if set, the object's flow "
                     "ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("sparse_infill_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Sparse infill flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 36;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for sparse infill. The actual sparse infill flow used is "
                     "calculated by multiplying this value by the filament flow ratio, and if set, the object's flow "
                     "ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("internal_solid_infill_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Internal solid infill flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 37;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for internal solid infill. The actual internal solid "
                     "infill flow used is calculated by multiplying this value by the filament flow ratio, and if set, "
                     "the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("gap_fill_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Gap fill flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 38;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for filling the gaps. The actual gap filling flow used "
                     "is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow "
                     "ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("support_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Support flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 39;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for support. The actual support flow used is calculated "
                     "by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("support_interface_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Support interface flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for the support interface. The actual support interface "
                     "flow used is calculated by multiplying this value by the filament flow ratio, and if set, the "
                     "object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("first_layer_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("First layer flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material on the first layer for the extrusion path roles listed "
                     "in this section. For the first layer, the actual flow ratio for each path role (does not affect "
                     "brims and skirts) will be multiplied by this value.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 3, air filtration (exhaust fan, M106 P3).
    def = defs.add("support_air_filtration", typeid(bool));
    def->location = Printer;
    def->label = L("Support air filtration");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 30;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable this if printer support air filtration\nG-code command: M106 P3 S(0-255)");
    def->init_fn = init_with(true);

    def = defs.add("activate_air_filtration", typeid(bool));
    def->location = Filament;
    def->label = L("Activate air filtration");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 30;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Activate for better air filtration. G-code command: M106 P3 S(0-255)");
    def->init_fn = init_with(false);

    def = defs.add("activate_air_filtration_during_print", typeid(bool));
    def->location = Filament;
    def->label = L("During print");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 31;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable this to override the fan speed set in custom G-code during print.");
    def->init_fn = init_with(true);

    def = defs.add("during_print_exhaust_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 32;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Speed of exhaust fan during printing. This speed will override the speed in filament custom G-code.");
    def->units = {"%"};
    def->min = 0;
    def->max = 100;
    def->full_label = L("Exhaust fan speed during print");
    def->init_fn = init_with(60);

    def = defs.add("activate_air_filtration_on_completion", typeid(bool));
    def->location = Filament;
    def->label = L("On completion");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 33;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable this to override the fan speed set in custom G-code after print completion.");
    def->init_fn = init_with(true);

    def = defs.add("complete_print_exhaust_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 34;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Speed of exhaust fan after printing completes.");
    def->units = {"%"};
    def->min = 0;
    def->max = 100;
    def->full_label = L("Exhaust fan speed on completion");
    def->init_fn = init_with(80);

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 12.
    def = defs.add("enable_overhang_bridge_fan", typeid(bool));
    def->location = Filament;
    def->label = L("Force cooling for overhangs and bridges");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable this option to allow adjustment of the part cooling fan speed for specifically for "
                     "overhangs, internal and external bridges. Setting the fan speed specifically for these features can "
                     "improve overall print quality and reduce warping.");
    def->init_fn = init_with(false);

    def = defs.add("overhang_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Overhangs and external bridges fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Use this part cooling fan speed when printing bridges or overhang walls with an overhang threshold "
                     "that exceeds the value set in the 'Overhangs cooling threshold' parameter above. Increasing the "
                     "cooling specifically for overhangs and bridges can improve the overall print quality of these "
                     "features. Please note, this fan speed is clamped on the lower end by the minimum fan speed "
                     "threshold set above. It is also adjusted upwards up to the maximum fan speed threshold when the "
                     "minimum layer time threshold is not met.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(100);

    def = defs.add("overhang_fan_threshold", typeid(EnumWrapper));
    def->location = Filament;
    def->label = L("Overhang cooling activation threshold");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 42;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("When the overhang exceeds this specified threshold, force the cooling fan to run at the "
                     "'Overhang Fan Speed' set below. This threshold is expressed as a percentage, indicating the "
                     "portion of each line's width that is unsupported by the layer beneath it. Setting this "
                     "value to 0% forces the cooling fan to run for all outer walls, regardless of the overhang "
                     "degree.");
    def->init_fn = init_with(
        OverhangFanThreshold::Bridge,
        {
            {int(OverhangFanThreshold::None),       "0%",  "0%"},
            {int(OverhangFanThreshold::Overhang10), "10%", "10%"},
            {int(OverhangFanThreshold::Overhang25), "25%", "25%"},
            {int(OverhangFanThreshold::Overhang50), "50%", "50%"},
            {int(OverhangFanThreshold::Overhang75), "75%", "75%"},
            {int(OverhangFanThreshold::Bridge),     "95%", "95%"},
        }
    );

    def = defs.add("scarf_joint_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->label = L("Scarf joint speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("This option sets the printing speed for scarf joints. It is recommended to print scarf joints "
                     "at a slow speed (less than 100 mm/s). It's also advisable to enable 'Extrusion rate smoothing' "
                     "if the set speed varies significantly from the speed of the outer or inner walls. If the "
                     "speed specified here is higher than the speed of the outer or inner walls, the printer will "
                     "default to the slower of the two speeds. When specified as a percentage (e.g., 80%), the speed "
                     "is calculated based on the respective outer or inner wall speed. The default value is set to "
                     "100%.");
    def->units = {L("mm/s"), L("%")};
    def->min = 1;
    // a percentage of the speed of the wall the joint is on: resolved in GCodeGenerator::_extrude
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));

    def = defs.add("scarf_joint_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Scarf joint flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of material for scarf joints.");
    def->min = 0;
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("time_cost", typeid(double));
    def->location = Printer;
    def->label = L("Time cost");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Advanced;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 60;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The printer cost per hour.");
    def->units = {L("money/h")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("enable_power_loss_recovery", typeid(EnumWrapper));
    def->location = Printer;
    def->label = L("Power Loss Recovery");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Advanced;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 61;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Choose how to control power loss recovery. When set to Printer configuration, the slicer will not "
                     "emit power loss recovery G-code and will leave the printer's configuration unchanged. Applicable to "
                     "Bambu Lab or Marlin 2 firmware based printers.");
    def->init_fn = init_with(
        PowerLossRecoveryMode::PrinterConfiguration,
        {
            {int(PowerLossRecoveryMode::PrinterConfiguration), "printer_configuration", L("Printer configuration")},
            {int(PowerLossRecoveryMode::Enable),               "enable",                L("Enable")},
            {int(PowerLossRecoveryMode::Disable),              "disable",               L("Disable")},
        }
    );

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 11, what Orca's runtime G-code variables read.
    def = defs.add("bed_mesh_min", typeid(Vec2d));
    def->location = Printer;
    def->label = L("Bed mesh min");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Advanced;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 50;
    def->gui_type = ConfigItemDef::GUIType::points;
    def->tooltip = L("This option sets the min point for the allowed bed mesh area. Due to the probe's XY offset, most "
                     "printers are unable to probe the entire bed. To ensure the probe point does not go outside the bed "
                     "area, the minimum and maximum points of the bed mesh should be set appropriately. OrcaSlicer "
                     "ensures that adaptive_bed_mesh_min/adaptive_bed_mesh_max values do not exceed these min/max points. "
                     "This information can usually be obtained from your printer manufacturer. The default setting is "
                     "(-99999, -99999), which means there are no limits, thus allowing probing across the entire bed.");
    def->units = {L("mm")};
    def->init_fn = init_with(Vec2d{-99999.0, -99999.0});

    def = defs.add("bed_mesh_max", typeid(Vec2d));
    def->location = Printer;
    def->label = L("Bed mesh max");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Advanced;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 51;
    def->gui_type = ConfigItemDef::GUIType::points;
    def->tooltip = L("This option sets the max point for the allowed bed mesh area. Due to the probe's XY offset, most "
                     "printers are unable to probe the entire bed. To ensure the probe point does not go outside the bed "
                     "area, the minimum and maximum points of the bed mesh should be set appropriately. OrcaSlicer "
                     "ensures that adaptive_bed_mesh_min/adaptive_bed_mesh_max values do not exceed these min/max points. "
                     "This information can usually be obtained from your printer manufacturer. The default setting is "
                     "(99999, 99999), which means there are no limits, thus allowing probing across the entire bed.");
    def->units = {L("mm")};
    def->init_fn = init_with(Vec2d{99999.0, 99999.0});

    def = defs.add("bed_mesh_probe_distance", typeid(Vec2d));
    def->location = Printer;
    def->label = L("Probe point distance");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Advanced;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 52;
    def->gui_type = ConfigItemDef::GUIType::points;
    def->tooltip = L("This option sets the preferred distance between probe points (grid size) for the X and Y directions, with the default being 50mm for both X and Y.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(Vec2d{50.0, 50.0});

    def = defs.add("adaptive_bed_mesh_margin", typeid(double));
    def->location = Printer;
    def->label = L("Mesh margin");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Advanced;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 53;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This option determines the additional distance by which the adaptive bed mesh area should be "
                     "expanded in the XY directions.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("support_chamber_temp_control", typeid(bool));
    def->location = Printer;
    def->label = L("Support controlling chamber temperature");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 35;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This option is enabled if machine support controlling chamber temperature\nG-code command: M141 S(0-255)");
    def->init_fn = init_with(true);

    def = defs.add("activate_chamber_temp_control", typeid(EnumWrapper));
    def->location = Filament;
    def->label = L("Activate temperature control");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Enable this option for automated chamber temperature control. This option activates the emitting of "
                     "an M191 command before the \"machine_start_gcode\" which sets the chamber temperature and waits "
                     "until it is reached. In addition, it emits an M141 command at the end of the print to turn off the "
                     "chamber heater, if present. This option relies on the firmware supporting the M191 and M141 "
                     "commands either via macros or natively and is usually used when an active chamber heater is "
                     "installed.");
    def->init_fn = init_with(
        ChamberTemperatureControl::Auto,
        {
            {int(ChamberTemperatureControl::Auto), "auto", L("Auto")},
            {int(ChamberTemperatureControl::Off),  "off",  L("Off")},
            {int(ChamberTemperatureControl::On),   "on",   L("On")},
        }
    );

    def = defs.add("temperature_vitrification", typeid(int));
    def->location = Filament;
    def->label = L("Softening temperature");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The material softens at this temperature, so when the bed temperature is equal to or greater than "
                     "this, it's highly recommended to open the front door and/or remove the upper glass to avoid clogs.");
    def->units = {L("°C")};
    def->min = 0;
    def->init_fn = init_with(100);

    def = defs.add("nozzle_temperature_range_low", typeid(int));
    def->location = Filament;
    def->label = L("Min");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_NozzleTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Lower end of the recommended nozzle temperature range of this filament.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = 1000;
    def->full_label = L("Recommended nozzle temperature: min");
    def->init_fn = init_with(190);

    def = defs.add("nozzle_temperature_range_high", typeid(int));
    def->location = Filament;
    def->label = L("Max");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_NozzleTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Upper end of the recommended nozzle temperature range of this filament.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = 1000;
    def->full_label = L("Recommended nozzle temperature: max");
    def->init_fn = init_with(240);

    def = defs.add("filament_flush_temp", typeid(int));
    def->location = Filament;
    def->label = L("Flush temperature");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Temperature when flushing filament. 0 indicates the upper bound of the recommended nozzle "
                     "temperature range.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = 500;
    def->init_fn = init_with(0);

    def = defs.add("filament_flush_volumetric_speed", typeid(double));
    def->location = Filament;
    def->label = L("Flush volumetric speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Volumetric speed when flushing filament. 0 indicates the max volumetric speed.");
    def->units = {L("mm³/s")};
    def->min = 0;
    def->max = 200;
    def->init_fn = init_with(0.);

    def = defs.add("filament_is_support", typeid(bool));
    def->location = Filament;
    def->label = L("Support material");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Support material is commonly used to print supports and support interfaces.");
    def->init_fn = init_with(false);

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 10.
    def = defs.add("file_start_gcode", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = -1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("File header G-code");
    def->tooltip = L("G-code written at the very top of the output file, before any other content. Useful for adding "
                     "metadata that printer firmware reads from the first lines of the file (e.g. estimated print time, "
                     "filament usage). Supports placeholders like {print_time_total_sec}, {print_time_day}, "
                     "{print_time_hour}, {print_time_minute}, {print_time_sec} and {used_filament_length}.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->init_fn = init_with("");

    def = defs.add("time_lapse_gcode", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 25;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Timelapse G-code");
    def->tooltip = L("This G-code is inserted at every layer change, after the Z lift (for a camera snapshot).");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->init_fn = init_with("");

    def = defs.add("timelapse_type", typeid(EnumWrapper));
    def->location = Print;
    def->label = L("Timelapse");
    def->option_group = ConfigItemDef::OptionGroup::Print_OutputOptions_OutputFile;
    def->category = ConfigItemDef::Category::Print_OutputOptions;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("If smooth or traditional mode is selected, a timelapse video will be generated for each print. "
                     "After each layer is printed, a snapshot is taken with the chamber camera. All of these snapshots "
                     "are composed into a timelapse video when printing completes. If smooth mode is selected, the "
                     "toolhead will move to the excess chute after each layer is printed and then take a snapshot. Since "
                     "the melt filament may leak from the nozzle during the process of taking a snapshot, a prime tower "
                     "is required for smooth mode to wipe the nozzle.");
    def->init_fn = init_with(
        TimelapseType::Traditional,
        {
            {int(TimelapseType::Traditional), "0", L("Traditional")},
            {int(TimelapseType::Smooth),      "1", L("Smooth")},
        }
    );

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 9, G-code output.
    def = defs.add("gcode_skip_config_block", typeid(bool));
    def->location = Printer;
    def->label = L("Skip G-code config block");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Advanced;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Do not write the CONFIG_BLOCK (slicer configuration key/value pairs) into the G-code file. This can "
                     "help with printers whose firmware crashes when parsing these comment lines (e.g. Anycubic "
                     "go-klipper). Note: the G-code file will no longer contain slicer settings, so importing it back "
                     "into OrcaSlicer will not restore the configuration.");
    def->init_fn = init_with(false);

    def = defs.add("gcode_add_line_number", typeid(bool));
    def->location = Print;
    def->label = L("Add line number");
    def->option_group = ConfigItemDef::OptionGroup::Print_OutputOptions_OutputFile;
    def->category = ConfigItemDef::Category::Print_OutputOptions;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable this to add line number(Nx) at the beginning of each G-code line.");
    def->init_fn = init_with(false);

    def = defs.add("manual_filament_change", typeid(bool));
    def->location = Printer;
    def->label = L("Manual Filament Change");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 33;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable this option to omit the custom Change filament G-code only at the beginning of the "
                     "print. The tool change command (e.g., T0) will be skipped throughout the entire print. This is "
                     "useful for manual multi-material printing, where we use M600/PAUSE to trigger the manual "
                     "filament change action.");
    def->init_fn = init_with(false);

    def = defs.add("bed_temperature_formula", typeid(EnumWrapper));
    def->location = Printer;
    def->label = L("Bed temperature type");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 34;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("This option determines how the bed temperature is set during slicing: based on the temperature "
                     "of the first filament or the highest temperature of the printed filaments.");
    def->init_fn = init_with(
        BedTemperatureFormula::ByFirstFilament,
        {
            {int(BedTemperatureFormula::ByFirstFilament), "by_first_filament", L("By First filament")},
            {int(BedTemperatureFormula::ByHighestTemp),   "by_highest_temp",   L("By Highest Temp")},
        }
    );

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 8, motion.
    def = defs.add("default_jerk", typeid(double));
    def->location = Print;
    def->label = L("Default");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Default jerk. 0 = jerk control off (the firmware's jerk is used).");
    def->units = {L("mm/s")};
    def->min = 0;
    def->full_label = L("Default jerk");
    def->init_fn = init_with(0.0);

    def = defs.add("outer_wall_jerk", typeid(double));
    def->location = Print;
    def->label = L("Outer wall");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Jerk of outer walls.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->full_label = L("Outer wall jerk");
    def->init_fn = init_with(9.0);

    def = defs.add("inner_wall_jerk", typeid(double));
    def->location = Print;
    def->label = L("Inner wall");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 42;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Jerk of inner walls.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->full_label = L("Inner wall jerk");
    def->init_fn = init_with(9.0);

    def = defs.add("infill_jerk", typeid(double));
    def->location = Print;
    def->label = L("Infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 43;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Jerk for infill.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->full_label = L("Infill jerk");
    def->init_fn = init_with(9.0);

    def = defs.add("top_surface_jerk", typeid(double));
    def->location = Print;
    def->label = L("Top surface");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 44;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Jerk for top surface.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->full_label = L("Top surface jerk");
    def->init_fn = init_with(9.0);

    def = defs.add("initial_layer_jerk", typeid(double));
    def->location = Print;
    def->label = L("First layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 45;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Jerk for the first layer.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->full_label = L("First layer jerk");
    def->init_fn = init_with(9.0);

    def = defs.add("travel_jerk", typeid(double));
    def->location = Print;
    def->label = L("Travel");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 46;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Jerk for travel.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->full_label = L("Travel jerk");
    def->init_fn = init_with(12.0);

    def = defs.add("initial_layer_travel_jerk", typeid(FloatOrPercentage));
    def->location = Print;
    def->label = L("First layer travel");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 47;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Travel jerk of first layer.\nThe percentage value is relative to Travel Jerk.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->full_label = L("First layer travel jerk");
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "travel_jerk";

    def = defs.add("initial_layer_travel_acceleration", typeid(FloatOrPercentage));
    def->location = Print;
    def->label = L("First layer travel");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_FirstLayerAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Travel acceleration of first layer.\nThe percentage value is relative to Travel Acceleration.");
    def->units = {L("mm/s²"), L("%")};
    def->min = 0;
    def->full_label = L("First layer travel acceleration");
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "travel_acceleration";

    def = defs.add("accel_to_decel_enable", typeid(bool));
    def->location = Print;
    def->label = L("Enable accel_to_decel");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 48;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Klipper's max_accel_to_decel will be adjusted automatically.");
    def->init_fn = init_with(false);

    def = defs.add("accel_to_decel_factor", typeid(Percentage));
    def->location = Print;
    def->label = L("accel_to_decel");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 49;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Klipper's max_accel_to_decel will be adjusted to this %% of acceleration.");
    def->units = {L("%")};
    def->min = 1;
    def->max = 100;
    def->init_fn = init_with(Percentage{50.});

    def = defs.add("default_junction_deviation", typeid(double));
    def->location = Print;
    def->label = L("Junction Deviation");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 50;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Marlin Firmware Junction Deviation (replaces the traditional XY Jerk setting).");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 0.3;
    def->init_fn = init_with(0.);

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 7, fans.
    def = defs.add("initial_layer_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("First layer fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Sets an exact fan speed for the first layer, overriding all other cooling settings. Useful for "
                     "protecting 3D-printed toolhead parts (e.g. Voron-style ABS/ASA ducts) from a hot bed. A small "
                     "amount of airflow cools the ducts down, without using full cooling that may in certain conditions "
                     "hurt first-layer adhesion. From the second layer onwards, normal cooling resumes. If \"Full fan "
                     "speed at layer\" is also set, the fan ramps smoothly from this value on the first layer up to your "
                     "target by the chosen layer. Only available when \"No cooling for the first\" is 0. Set to -1 to "
                     "disable it.");
    def->units = {L("%")};
    def->min = -1;
    def->max = 100;
    def->init_fn = init_with(-1);

    def = defs.add("support_material_interface_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Support interface fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This part cooling fan speed is applied when printing support interfaces. Setting this parameter to "
                     "a higher than regular speed reduces the layer binding strength between supports and the supported "
                     "part, making them easier to separate. Set to -1 to disable it. This setting is overridden by "
                     "disable_fan_first_layers.");
    def->units = {L("%")};
    def->min = -1;
    def->max = 100;
    def->init_fn = init_with(-1);

    def = defs.add("internal_bridge_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Internal bridges fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 41;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The part cooling fan speed used for all internal bridges. Set to -1 to use the overhang fan speed "
                     "settings instead. Reducing the internal bridges fan speed, compared to your regular fan speed, can "
                     "help reduce part warping due to excessive cooling applied over a large surface for a prolonged "
                     "period of time.");
    def->units = {L("%")};
    def->min = -1;
    def->max = 100;
    def->init_fn = init_with(-1);

    def = defs.add("ironing_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Ironing fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 42;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This part cooling fan speed is applied when ironing. Setting this parameter to a lower than regular speed reduces possible nozzle clogging due to the low volumetric flow rate, making the interface smoother.\nSet to -1 to disable it.");
    def->units = {L("%")};
    def->min = -1;
    def->max = 100;
    def->init_fn = init_with(-1);

    def = defs.add("additional_cooling_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 43;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Speed of auxiliary part cooling fan. Auxiliary fan will run at this speed during printing except "
                     "the first several layers which is defined by no cooling layers. Please enable auxiliary_fan in "
                     "printer settings to use this feature. G-code command: M106 P2 S(0-255)");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->full_label = L("Auxiliary fan speed");
    def->init_fn = init_with(0);

    def = defs.add("dont_slow_down_outer_wall", typeid(bool));
    def->location = Filament;
    def->label = L("Don't slow down outer walls");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingLogic;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 40;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, this setting will ensure external perimeters are not slowed down to meet the "
                     "minimum layer time. This is particularly helpful in the below scenarios:\n\n"
                     "1. To avoid changes in shine when printing glossy filaments\n"
                     "2. To avoid changes in external wall speed which may create slight wall artifacts that "
                     "appear like Z banding\n"
                     "3. To avoid printing at speeds which cause VFAs (fine artifacts) on the external walls");
    def->init_fn = init_with(false);

    def = defs.add("auxiliary_fan", typeid(bool));
    def->location = Printer;
    def->label = L("Auxiliary part cooling fan");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 31;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable this option if machine has auxiliary part cooling fan. G-code command: M106 P2 S(0-255).");
    def->init_fn = init_with(false);

    def = defs.add("part_cooling_fan_min_pwm", typeid(int));
    def->location = Printer;
    def->label = L("Minimum non-zero part cooling fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 32;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Some part-cooling fans cannot start spinning when commanded below a certain PWM duty cycle. When "
                     "set above 0, any non-zero part-cooling fan command will be raised to at least this percentage so "
                     "the fan reliably starts. A fan command of 0 (fan off) is always honoured exactly. This clamp is "
                     "applied after every other fan calculation (first-layer ramp, layer-time interpolation, "
                     "overhang/bridge/support-interface/ironing overrides), so scaling still operates within the range "
                     "[this value, 100%]. If your firmware already disables the fan below a threshold (for example "
                     "Klipper's [fan] off_below: 0.10 shuts the fan off whenever the commanded duty cycle is below 10%), "
                     "this option and the firmware threshold should ideally be set to the same value. Matching them (e.g. "
                     "off_below: 0.10 in Klipper and 10% here) guarantees the slicer never emits a non-zero value that "
                     "the firmware would silently drop, and the fan never receives a value below the one you know it can "
                     "actually spool at. Set to 0 to deactivate.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(0);

    def = defs.add("fan_speedup_time", typeid(double));
    def->location = Printer;
    def->label = L("Fan speed-up time");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 33;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Start the fan this number of seconds earlier than its target start time (you can use fractional "
                     "seconds). It assumes infinite acceleration for this time estimation, and will only take into "
                     "account G1 and G0 moves (arc fitting is unsupported). It won't move fan commands from custom G-code "
                     "(they act as a sort of 'barrier'). It won't move fan commands into the start G-code if the 'only "
                     "custom start G-code' is activated. Use 0 to deactivate.");
    def->units = {L("s")};
    def->init_fn = init_with(0.);

    def = defs.add("fan_speedup_overhangs", typeid(bool));
    def->location = Printer;
    def->label = L("Only overhangs");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 34;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Will only take into account the delay for the cooling of overhangs.");
    def->init_fn = init_with(true);

    def = defs.add("fan_kickstart", typeid(double));
    def->location = Printer;
    def->label = L("Fan kick-start time");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 35;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Emit a max fan speed command for this amount of seconds before reducing to target speed to "
                     "kick-start the cooling fan. This is useful for fans where a low PWM/power may be insufficient to "
                     "get the fan started spinning from a stop, or to get the fan up to speed faster. Set to 0 to "
                     "deactivate.");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 6.
    def = defs.add("xy_hole_compensation", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("X-Y hole compensation");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 20;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Holes in objects will expand or contract in the XY plane by the set value. Positive values "
                     "make holes bigger and negative values make holes smaller. This function is used to adjust "
                     "sizes slightly when objects have assembly issues.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("elefant_foot_compensation_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Elephant foot compensation layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 21;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The number of layers on which the elephant foot compensation will be active. The first "
                     "layer will be shrunk by the elephant foot compensation value, then the next layers will be "
                     "linearly shrunk less, up to the layer indicated by this value.");
    def->units = {L("layers")};
    def->min = 1;
    def->init_fn = init_with(1);

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 5. 3.0 used a fixed 6.5 mm.
    def = defs.add("small_perimeter_threshold", typeid(double));
    def->location = Print;
    def->label = L("Small perimeters threshold");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 30;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This sets the threshold for small perimeter length (as the radius of a circle of "
                     "that length). Perimeters up to it are printed at the small perimeter speed.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(6.5);

    // Ported from OrcaSlicer (PrintConfig.cpp): parity batch 4.
    def = defs.add("slow_down_layers", typeid(int));
    def->location = Print;
    def->label = L("Number of slow layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 21;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The first few layers are printed slower than normal. "
                     "The speed is gradually increased in a linear fashion over the specified number of layers.");
    def->units = {L("layers")};
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("skirt_distance", typeid(double));
    def->location = Print;
    def->label = L("Distance from brim/object");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Distance between skirt and brim (when draft shield is not used) or objects.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(6.);

    def = defs.add("skirt_height", typeid(int));
    def->location = Print;
    def->label = L("Skirt height");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Height of skirt expressed in layers.");
    def->units = {L("layers")};
    def->init_fn = init_with(1);

    def = defs.add("skirt_start_angle", typeid(double));
    def->location = Print;
    def->label = L("Skirt start point");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Angle from the object center to skirt start point. Zero is the most right position, counter "
                     "clockwise is positive angle.");
    def->units = {L("°")};
    def->min = -180;
    def->max = 180;
    def->init_fn = init_with(-135.);

    def = defs.add("single_loop_draft_shield", typeid(bool));
    def->location = Print;
    def->label = L("Single loop after first layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Limits the skirt/draft shield loops to one wall after the first layer. This is useful, on occasion, "
                     "to conserve filament but may cause the draft shield/skirt to warp / crack.");
    def->init_fn = init_with(false);

    def = defs.add("draft_shield", typeid(EnumWrapper));
    def->location = Print;
    def->label = L("Draft shield");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("With draft shield active, the skirt will be printed skirt_distance from the object, possibly intersecting brim.\n"
                     "Enabled = skirt is as tall as the highest printed object.\n"
                     "Limited = skirt is as tall as specified by skirt_height.\n"
    				 "This is useful to protect an ABS or ASA print from warping and detaching from print bed due to wind draft.");
    def->init_fn = init_with(
        DraftShield::dsDisabled,
        {{int(DraftShield::dsDisabled), "disabled", L("Disabled")},
         {int(DraftShield::dsLimited), "limited", L("Limited")},
         {int(DraftShield::dsEnabled), "enabled", L("Enabled")}}
    );

    def = defs.add("skirt_type", typeid(EnumWrapper));
    def->location = Print;
    def->label = L("Skirt type");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Combined - single skirt for all objects, Per object - individual object skirt.");
    def->init_fn = init_with(
        SkirtType::Combined,
        {{int(SkirtType::Combined), "combined", L("Combined")},
         {int(SkirtType::PerObject), "perobject", L("Per object")}}
    );

    def = defs.add("skirts", typeid(int));
    def->location = Print;
    def->label = L("Loops (minimum)");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("Skirt Loops");
    def->tooltip = L("Number of loops for the skirt. If the Minimum Extrusion Length option is set, "
                   "the number of loops might be greater than the one configured here. Set this to zero "
                   "to disable skirt completely.");
    def->min = 0;
    def->init_fn = init_with(1);

    def = defs.add("slowdown_below_layer_time", typeid(int));
    def->location = Filament;
    def->label = L("Slow down if layer print time is below");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingThresholds;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("If layer print time is estimated below this number of seconds, print moves "
                   "speed will be scaled down to extend duration to this value.");
    def->units = {L("s")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(5);

    def = defs.add("small_perimeter_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Small perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("This separate setting will affect the speed of perimeters having radius <= 6.5mm "
                   "(usually holes). If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{15.});
    def->ratio_over = "perimeter_speed";

    def = defs.add("solid_infill_below_area", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Solid infill threshold area");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Force solid infill for regions having a smaller area than the specified threshold.");
    def->units = {L("mm²")};
    def->min = 0;
    def->init_fn = init_with(70.);

    def = defs.add("solid_infill_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Solid infill extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing solid infill.");
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("solid_infill_every_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Solid infill every");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This feature allows to force a solid layer every given number of layers. "
                   "Zero to disable. You can set this to any value (for example 9999); "
                   "Slic3r will automatically choose the maximum possible number of layers "
                   "to combine according to nozzle diameter and layer height.");
    def->units = {L("layers")};
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("bridge_line_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Bridge");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Line width of the Bridge. If expressed as a %, it will be computed over the nozzle diameter. "
                     "Recommended to use with a higher Bridge density or Bridge flow ratio. The maximum value is 100% or "
                     "the nozzle diameter. If set to 0, the line width will match the Internal solid infill width, or the "
                     "nozzle diameter for thick bridges.");
    def->units = {L("mm"), L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("solid_infill_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for solid surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("solid_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Speed for printing solid regions (top/bottom/internal horizontal shells). "
                   "This can be expressed as a percentage (for example: 80%) over the default "
                   "infill speed above. Set to zero for auto.");
    def->units = {L("mm/s"), L("%")};
    def->aliases = { "solid_infill_feed_rate" };
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{20.});
    def->ratio_over = "infill_speed";
    
    /* TODO : shortcut - maybe remove it?
    def = defs.add("solid_layers", typeid(int));
    def->label = L("Solid layers");
    def->tooltip = L("Number of solid layers to generate on top and bottom surfaces.");
    def->shortcut.push_back("top_solid_layers");
    def->shortcut.push_back("bottom_solid_layers");
    def->min = 0;

    def = defs.add("solid_min_thickness", typeid(double));
    def->label = L("Minimum thickness of a top / bottom shell");
    def->tooltip = L("Minimum thickness of a top / bottom shell");
    def->shortcut.push_back("top_solid_min_thickness");
    def->shortcut.push_back("bottom_solid_min_thickness");
    def->min = 0;*/

    def = defs.add("spiral_vase", typeid(bool));
    def->location = Print;
    def->label = L("Spiral vase");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This feature will raise Z gradually while printing a single-walled object "
                   "in order to remove any visible seam. This option requires a single perimeter, "
                   "no infill, no top solid layers and no support material. You can still set "
                   "any number of bottom solid layers as well as skirt/brim loops. "
                   "It won't work when printing more than one single object.");
    def->init_fn = init_with(false);

    def = defs.add("spiral_mode_smooth", typeid(bool));
    def->location = Print;
    def->label = L("Smooth Spiral");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Smooth Spiral smooths out X and Y moves as well, resulting in no visible seam at all, even in the "
                     "XY directions on walls that are not vertical.");
    def->init_fn = init_with(true);

    def = defs.add("spiral_mode_max_xy_smoothing", typeid(FloatOrPercentage));
    def->location = Print;
    def->label = L("Max XY Smoothing");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Maximum distance to move points in XY to try to achieve a smooth spiral. If expressed as a %, it "
                     "will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};
    def->min = 0;
    def->max = 1000;
    // a percentage of the nozzle diameter: resolved in SpiralVase
    def->init_fn = init_with(FloatOrPercentage(Percentage{200.}));

    def = defs.add("spiral_starting_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Spiral starting flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Sets the starting flow ratio while transitioning from the last bottom layer to the spiral. Normally "
                     "the spiral transition scales the flow ratio from 0% to 100% during the first loop which can in some "
                     "cases lead to under extrusion at the start of the spiral.");
    def->min = 0;
    def->max = 1;
    def->init_fn = init_with(0.);

    def = defs.add("spiral_finishing_flow_ratio", typeid(double));
    def->location = Print;
    def->label = L("Spiral finishing flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Sets the finishing flow ratio while ending the spiral. Normally the spiral transition scales the "
                     "flow ratio from 100% to 0% during the last loop which can in some cases lead to under extrusion at "
                     "the end of the spiral.");
    def->min = 0;
    def->max = 1;
    def->init_fn = init_with(0.);

    def = defs.add("standby_temperature_delta", typeid(int));
    def->location = Print;
    def->label = L("Temperature variation");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_OozePrevention;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    // TRN PrintSettings : "Ooze prevention" > "Temperature variation"
    def->tooltip = L("Temperature difference to be applied when an extruder is not active. "
                     "The value is not used when 'idle_temperature' in filament settings "
                     "is defined.");
    def->units = {"∆°C"};
    def->min = -max_temp;
    def->max = max_temp;
    def->init_fn = init_with(-5);

    def = defs.add("preheat_time", typeid(double));
    def->location = Print;
    def->label = L("Preheat time");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_OozePrevention;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("To reduce the waiting time after tool change, Orca can preheat the next tool while the current tool "
                     "is still in use. This setting specifies the time in seconds to preheat the next tool. Orca will "
                     "insert a M104 command to preheat the tool in advance.");
    def->units = {L("s")};
    def->min = 0;
    def->max = 120;
    def->init_fn = init_with(30.);

    def = defs.add("preheat_steps", typeid(int));
    def->location = Print;
    def->label = L("Preheat steps");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_OozePrevention;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Insert multiple preheat commands (e.g. M104.1). Only useful for Prusa XL. For other printers, "
                     "please set it to 1.");
    def->min = 1;
    def->max = 10;
    def->init_fn = init_with(1);

    def = defs.add("autoemit_temperature_commands", typeid(bool));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->order = 1;
    def->label = L("Emit temperature commands automatically");
    def->tooltip = L("When enabled, PiggieSlicer will check whether your custom Start G-code contains G-codes to set "
                     "extruder, bed or chamber temperature (M104, M109, M140, M190, M141 and M191). "
                     "If so, the temperatures will not be emitted automatically so you're free to customize "
                     "the order of heating commands and other custom actions. Note that you can use "
                     "placeholder variables for all PiggieSlicer settings, so you can put "
                     "a \"M109 S[first_layer_temperature]\" command wherever you want.\n"
                     "If your custom Start G-code does NOT contain these G-codes, "
                     "PiggieSlicer will execute the Start G-code after heated chamber was set to its temperature, "
                     "bed reached its target temperature and extruder just started heating.\n\n"
                     "When disabled, PiggieSlicer will NOT emit commands to heat up extruder, bed or chamber, "
                     "leaving all to Custom Start G-code.");
    def->init_fn = init_with(true);

    def = defs.add("start_gcode", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Start G-code");
    def->tooltip = L("This start procedure is inserted at the beginning, possibly prepended by "
                     "temperature-changing commands. See 'autoemit_temperature_commands'.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n");

    def = defs.add("start_filament_gcode", typeid(std::string));
    def->location = Filament;
    def->label = L("Start G-code");
    def->category = ConfigItemDef::Category::Filament_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Filament_CustomGCode;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This start procedure is inserted at the beginning, after any printer start gcode (and "
                   "after any toolchange to this filament in case of multi-material printers). "
                   "This is used to override settings for a specific filament. If PiggieSlicer detects "
                   "M104, M109, M140 or M190 in your custom codes, such commands will "
                   "not be prepended automatically so you're free to customize the order "
                   "of heating commands and other custom actions. Note that you can use placeholder variables "
                   "for all PiggieSlicer settings, so you can put a \"M109 S[first_layer_temperature]\" command "
                   "wherever you want. If you have multiple extruders, the gcode is processed "
                   "in extruder order.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("; Filament gcode\n");

    def = defs.add("color_change_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("Color change G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This G-code will be used as a code for the color change");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("M600");

    def = defs.add("pause_print_gcode", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Pause Print G-code");
    def->tooltip = L("This G-code will be used as a code for the pause print");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("M601");

    def = defs.add("template_custom_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("Custom G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This G-code will be used as a custom code");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("");

    def = defs.add("single_extruder_multi_material", typeid(bool));
    def->location = Printer;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Single Extruder Multi Material");
    def->tooltip = L("The printer multiplexes filaments into a single hot end.");
    def->init_fn = init_with(false);

    def = defs.add("single_extruder_multi_material_priming", typeid(bool));
    def->location = Print;
    def->label = L("Prime all printing extruders");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, all printing extruders will be primed at the front edge of the print bed at the start of the print.");
    def->init_fn = init_with(true);

    def = defs.add("wipe_tower_no_sparse_layers", typeid(bool));
    def->location = Print;
    def->label = L("No sparse layers (EXPERIMENTAL)");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, the wipe tower will not be printed on layers with no toolchanges. "
                     "On layers with a toolchange, extruder will travel downward to print the wipe tower. "
                     "User is responsible for ensuring there is no collision with the print.");
    def->init_fn = init_with(false);

    def               = defs.add("toolchange_ordering", typeid(EnumWrapper));
    def->location     = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in       = Locations{Tool};
    def->label        = L("Toolchange ordering");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ToolChanges;
    def->category     = ConfigItemDef::Category::Print_MultiMaterial;
    def->order        = 0;
    def->gui_type     = ConfigItemDef::GUIType::combobox;
    def->tooltip      = L(
        "Determines the order of tool changes on each layer.\n"
        "Optimized - Starts with the last used extruder to minimize tool changes.\n"
        "Cyclic - Uses extruders in a fixed sequential order (1, 2, 3, ...) on every layer."
    );
    def->init_fn = init_with(
        ToolChangeOrderingType::Optimized,
        {
            {int(ToolChangeOrderingType::Optimized), "optimized", L("Optimized")},
            {int(ToolChangeOrderingType::Cyclic),    "cyclic",    L("Cyclic")},
        }
    );

    def               = defs.add("toolchange_cyclic_order", typeid(std::string));
    def->location     = Print;
    def->label        = L("Cyclic order");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ToolChanges;
    def->category     = ConfigItemDef::Category::Print_MultiMaterial;
    def->order        = 1;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Custom filament sequence used by the cyclic toolchange ordering, as filament numbers separated by "
                     "commas (e.g. \"3,2,1,4\"). Each layer prints its filaments following this sequence; filaments not "
                     "listed are printed last, in ascending order. Leave empty to cycle through the filaments in "
                     "ascending order.");
    def->init_fn = init_with(std::string{});

    def               = defs.add("toolchange_cyclic_first_layer", typeid(bool));
    def->location     = Print;
    def->label        = L("Apply cyclic order to first layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ToolChanges;
    def->category     = ConfigItemDef::Category::Print_MultiMaterial;
    def->order        = 2;
    def->gui_type     = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Applies the cyclic toolchange order to the first layer as well. By default this is disabled, "
                     "because the first layer is instead ordered for the best bed adhesion: filaments that print small, "
                     "fragile first-layer features are printed last, so the following tool changes and travel moves are "
                     "less likely to knock those weakly anchored parts loose. This first-layer order also honors a custom "
                     "first layer filament sequence when one is set. The cyclic order's benefit (extra tool changes give "
                     "each layer more time to cool) does not apply to the first layer, which is printed slowly and hot "
                     "for adhesion. Enable this only if you need the exact same tool sequence on every layer, including "
                     "the first, at the cost of that adhesion optimization.");
    def->init_fn = init_with(false);

    def               = defs.add("support_material", typeid(EnumWrapper));
    def->location     = Print;
    def->overrides_in = Locations{Object};
    def->label        = L("Generate Supports");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category     = ConfigItemDef::Category::Print_Supports;
    def->order        = 0;
    def->gui_type     = ConfigItemDef::GUIType::combobox;
    def->tooltip      = L(
        "Enable support material generation.\n"
             "Off - Disables support material generation.\n"
             "Manual Only - Generates supports inside the \"Support Enforcer\" volumes only.\n"
             "On - Generates supports automatically based on the overhang threshold value and also inside the \"Support Enforcer\" volumes."
    );
    def->init_fn = init_with(
        SupportMode::EnforcersOnly,
        {{int(SupportMode::None), "none", L("Off")},
         {int(SupportMode::EnforcersOnly), "enforcers_only", L("Manual Only")},
         {int(SupportMode::Everywhere), "everywhere", L("On")}}
    );

    def = defs.add("support_material_xy_spacing", typeid(FloatOrPercentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("XY separation between an object and its support");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("XY separation between an object and its support. If expressed as percentage "
                   "(for example 50%), it will be calculated over external perimeter width.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 10;
    // Default is half the external perimeter width.
    def->init_fn = init_with(FloatOrPercentage(Percentage{50.}));
    def->ratio_over = "external_perimeter_extrusion_width";

    def = defs.add("support_material_angle", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Pattern angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_PatternDensity;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Use this setting to rotate the support material pattern on the horizontal plane.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 359;
    def->init_fn = init_with(0.);

    def = defs.add("support_material_buildplate_only", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support on build plate only");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Only create support if it lies on a build plate. Don't create support on a print.");
    def->init_fn = init_with(false);

    def = defs.add("support_material_contact_distance", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Top contact Z distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("The vertical distance between object and support material interface. "
                   "Setting this to 0 will also prevent Slic3r from using bridge flow and speed "
                   "for the first object layer.");
    def->units = {L("mm")};
    def->choices = {
        { 0.,     L("0 (soluble)") },
        { 0.1,    L("0.1 (detachable)") },
        { 0.2,    L("0.2 (detachable)") }
    };
    def->init_fn = init_with(0.2);

    def = defs.add("support_material_bottom_contact_distance", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Bottom contact Z distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("The vertical distance between the object top surface and the support material interface. "
                   "If set to zero, support_material_contact_distance will be used for both top and bottom contact Z distances.");
    def->units = {L("mm")};
    def->choices = {
        //TRN Print Settings: "Bottom contact Z distance". Have to be as short as possible
        { 0.,     L("Same as top") },
        { 0.1,    "0.1" },
        { 0.2,    "0.2" }
    };
    def->init_fn = init_with(0.);

    def = defs.add("support_material_enforce_layers", typeid(int));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Enforce support for the first");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Generate support material for the specified number of layers counting from bottom, "
                   "regardless of whether normal support material is enabled or not and regardless "
                   "of any angle threshold. This is useful for getting more adhesion of objects "
                   "having a very thin or poor footprint on the build plate.");
    def->units = {L("layers")};
    def->full_label = L("Enforce support for the first n layers");
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("support_material_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support material/raft/skirt extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing support material, raft and skirt "
                   "(1+, 0 to use the current extruder to minimize tool changes).");
    def->min = 0;
    def->init_fn = init_with(1);

    def = defs.add("support_material_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Support material");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for support material. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def           = defs.add("support_material_first_layer_density", typeid(Percentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in               = Locations{Tool, Object};
    def->label                      = L("First layer density");
    def->option_group               = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category                   = ConfigItemDef::Category::Print_Supports;
    def->order                      = 0;
    def->gui_type                   = ConfigItemDef::GUIType::textfield;
    def->tooltip                    = L("Density of the first support layer.");
    def->units                      = {L("%")};
    def->min                        = 10;
    def->max                        = 100;
    def->init_fn                    = init_with(Percentage{90.});

    def           = defs.add("support_material_first_layer_expansion", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in               = Locations{Tool, Object};
    def->label                      = L("First layer expansion");
    def->option_group               = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category                   = ConfigItemDef::Category::Print_Supports;
    def->order                      = 1;
    def->gui_type                   = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("Expansion of the support first layer to improve adhesion to print bed.");
    def->units = {L("mm")};
    def->min      = 0;
    def->init_fn  = init_with(3.);

    def = defs.add("support_material_interface_contact_loops", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Interface loops");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Cover the top contact layer of the supports with loops. Disabled by default.");
    def->init_fn = init_with(false);

    def = defs.add("support_material_interface_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support material/raft interface extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing support material interface "
                   "(1+, 0 to use the current extruder to minimize tool changes). This affects raft too.");
    def->min = 0;
    def->init_fn = init_with(1);

    def = defs.add("support_material_interface_layers", typeid(int));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Top interface layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::i_enum_open;
    def->tooltip = L("Number of interface layers to insert between the object(s) and support material.");
    def->units = {L("layers")};
    def->min = 0;
    def->choices = {
        { 0, L("0 (off)") },
        { 1, L("1 (light)") },
        { 2, L("2 (default)") },
        { 3, L("3 (heavy)") }
    };
    def->init_fn = init_with(3);

    def = defs.add("support_material_bottom_interface_layers", typeid(int));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Bottom interface layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::i_enum_open;
    def->tooltip = L("Number of interface layers to insert between the object(s) and support material. "
                     "Set to -1 to use support_material_interface_layers");
    def->units = {L("layers")};
    def->min = -1;
    def->choices = {
        //TRN Print Settings: "Bottom interface layers". Have to be as short as possible
        { -1, L("Same as top") },
        { 0,  L("0 (off)") },
        { 1,  L("1 (light)") },
        { 2,  L("2 (default)") },
        { 3,  L("3 (heavy)") }
    };
    def->init_fn = init_with(-1); 

    def = defs.add("support_material_closing_radius", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Closing radius");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("For snug supports, the support regions will be merged using morphological closing operation."
                     " Gaps smaller than the closing radius will be filled in.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(2.);

    def = defs.add("support_material_interface_spacing", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Interface pattern spacing");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Spacing between interface lines. Set zero to get a solid interface.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("support_material_interface_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Support material interface");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Speed for printing support material interface layers. If expressed as percentage "
                   "(for example 50%) it will be calculated over support material speed.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "support_material_speed";

    def = defs.add("support_material_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_PatternDensity;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Pattern used to generate support material.");
    def->init_fn = init_with(
        SupportMaterialPattern::smpRectilinear,
        {{int(SupportMaterialPattern::smpRectilinear), "rectilinear", L("Rectilinear")},
         {int(SupportMaterialPattern::smpRectilinearGrid), "rectilinear-grid", L("Rectilinear grid")},
         {int(SupportMaterialPattern::smpHoneycomb), "honeycomb", L("Honeycomb")}}
    );

    def = defs.add("support_threshold_overlap", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Threshold overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If threshold angle is zero, support will be generated for overhangs whose overlap is below the threshold. The smaller this value is, the steeper the overhang that can be printed without support.");
    def->units = {L("mm"), L("%")};
    def->min = 0;
    def->max = 100;
    def->ratio_over = "external_perimeter_extrusion_width";
    def->init_fn = init_with(FloatOrPercentage(Percentage{50.}));

    def = defs.add("support_remove_small_overhang", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Ignore small overhangs");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Ignore small overhangs that possibly don't require support.");
    // off: 3.0's supports unchanged unless asked for (Orca: on)
    def->init_fn = init_with(false);

    def = defs.add("support_expansion", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Normal support expansion");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Expand (+) or shrink (-) the horizontal span of normal support.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("support_object_first_layer_gap", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support/object first layer gap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("XY separation between an object and its support at the first layer.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 10;
    def->init_fn = init_with(0.2);

    def = defs.add("support_bottom_interface_spacing", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Bottom interface spacing");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the spacing of bottom interface lines. 0 means solid interface.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.5);

    def = defs.add("support_ironing", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Ironing Support Interface");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Ironing is using small flow to print on same height of support interface again to make it more smooth. This setting controls whether support interface being ironed. When enabled, support interface will be extruded as solid too.");
    def->init_fn = init_with(false);

    def = defs.add("support_ironing_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support Ironing Pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("The pattern that will be used when ironing.");
    def->init_fn = init_with(
        InfillPattern::ipRectilinear,
        {{int(InfillPattern::ipRectilinear), "rectilinear", L("Rectilinear")},
         {int(InfillPattern::ipConcentric), "concentric", L("Concentric")}}
    );

    def = defs.add("support_ironing_flow", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support Ironing flow");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 11;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The amount of material to extrude during ironing. Relative to flow of normal support interface layer height. Too high value results in overextrusion on the surface.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(Percentage{10.});

    def = defs.add("support_ironing_spacing", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support Ironing line spacing");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 12;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the distance between the lines used for ironing.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 1;
    def->init_fn = init_with(0.1);

    def = defs.add("support_interface_not_for_body", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Avoid interface filament for base");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Avoid using support interface filament to print support base if possible.");
    def->init_fn = init_with(true);

    def = defs.add("support_material_interface_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Interface pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Pattern used to generate support material interface. "
                     "Default pattern for non-soluble support interface is Rectilinear, "
                     "while default pattern for soluble support interface is Concentric.");
    def->init_fn = init_with(
        SupportMaterialInterfacePattern::smipRectilinear,
        {{int(SupportMaterialInterfacePattern::smipAuto), "auto", L("Default")},
         {int(SupportMaterialInterfacePattern::smipRectilinear), "rectilinear", L("Rectilinear")},
         {int(SupportMaterialInterfacePattern::smipConcentric), "concentric", L("Concentric")},
         {int(SupportMaterialInterfacePattern::smipSpiralInset), "spiralinset", L("Spiral Inset")},
         {int(SupportMaterialInterfacePattern::smipRectilinearInterlaced), "rectilinear_interlaced", L("Rectilinear Interlaced")},
         {int(SupportMaterialInterfacePattern::smipGrid), "grid", L("Grid")}}
    );

    def = defs.add("support_material_spacing", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Pattern spacing");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_PatternDensity;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Spacing between support material lines.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(2.5);

    def = defs.add("support_material_speed", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Support material");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for printing support material.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(60.);

    def = defs.add("small_support_perimeter_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Small support perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Same as \"Small perimeters\", but for supports. This separate setting will affect the speed of "
                     "support for areas <= `small_support_perimeter_threshold`. If expressed as a percentage (for "
                     "example: 80%), it will be calculated on the support or support interface speed setting above.");
    def->units = {L("mm/s"), L("%")};
    def->min = 1;
    // a percentage of the support or support interface speed: resolved in GCodeGenerator::extrude_support
    def->init_fn = init_with(FloatOrPercentage(Percentage{50.}));

    def = defs.add("small_support_perimeter_threshold", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Small support perimeters threshold");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This sets the threshold for small support perimeter length. The default threshold is 0mm.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("support_material_style", typeid(EnumWrapper));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Style");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Style and shape of the support towers. Projecting the supports into a regular grid "
                     "will create more stable supports, while snug support towers will save material and reduce "
                     "object scarring.");
    def->init_fn = init_with(
        SupportMaterialStyle::smsGrid,
        {{int(SupportMaterialStyle::smsGrid), "grid", L("Grid")},
         {int(SupportMaterialStyle::smsSnug), "snug", L("Snug")},
         {int(SupportMaterialStyle::smsOrganic), "organic", L("Organic")}}
    );

    def = defs.add("support_material_synchronize_layers", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Synchronize with object layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    // TRN PrintSettings : "Synchronize with object layers"
    def->tooltip = L("Synchronize support layers with the object print layers. This is useful "
                   "with multi-material printers, where the extruder switch is expensive. "
                   "This option is only available when top contact Z distance is set to zero.");
    def->init_fn = init_with(false);

    def = defs.add("support_material_threshold", typeid(int));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Overhang threshold");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Support material will not be generated for overhangs whose slope angle "
                   "(90° = vertical) is above the given threshold. In other words, this value "
                   "represent the most horizontal slope (measured from the horizontal plane) "
                   "that you can print without support material. Set to zero for automatic detection "
                   "(recommended).");
    def->units = {L("°")};
    def->min = 0;
    def->max = 90;
    def->init_fn = init_with(0);

    def = defs.add("support_material_with_sheath", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("With sheath around the support");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Add a sheath (a single perimeter line) around the base support. This makes "
                   "the support more reliable, but also more difficult to remove.");
    def->init_fn = init_with(true);

    def = defs.add("support_tree_angle", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Maximum Branch Angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Maximum Branch Angle"
    def->tooltip = L("The maximum angle of the branches, when the branches have to avoid the model. "
                     "Use a lower angle to make them more vertical and more stable. Use a higher angle to be able to have more reach.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 85;
    def->init_fn = init_with(40.);

    def = defs.add("support_tree_angle_slow", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Preferred Branch Angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Preferred Branch Angle"
    def->tooltip = L("The preferred angle of the branches, when they do not have to avoid the model. "
                     "Use a lower angle to make them more vertical and more stable. Use a higher angle for branches to merge faster.");
    def->units = {L("°")};
    def->min = 10;
    def->max = 85;
    def->init_fn = init_with(25.);

    def = defs.add("support_tree_tip_diameter", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Tip Diameter");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Tip Diameter"
    def->tooltip = L("Branch tip diameter for organic supports.");
    def->units = {L("mm")};
    def->min = 0.1f;
    def->max = 100.f;
    def->init_fn = init_with(0.8);

    def = defs.add("support_tree_branch_diameter", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Diameter");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Diameter"
    def->tooltip = L("The diameter of the thinnest branches of organic support. Thicker branches are more sturdy. "
                     "Branches towards the base will be thicker than this.");
    def->units = {L("mm")};
    def->min = 0.1f;
    def->max = 100.f;
    def->init_fn = init_with(2.);

    def = defs.add("support_tree_branch_diameter_angle", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Diameter Angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Diameter Angle"
    def->tooltip = L("The angle of the branches' diameter as they gradually become thicker towards the bottom. "
                     "An angle of 0 will cause the branches to have uniform thickness over their length. "
                     "A bit of an angle can increase stability of the organic support.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 15;
    def->init_fn = init_with(5.);

    def = defs.add("support_tree_branch_diameter_double_wall", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Diameter with double walls");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Diameter"
    def->tooltip = L("Branches with area larger than the area of a circle of this diameter will be printed with double walls for stability. "
                     "Set this value to zero for no double walls.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 100.f;
    def->init_fn = init_with(3.);

    // Tree Support Branch Distance
    // How far apart the branches need to be when they touch the model. Making this distance small will cause
    // the tree support to touch the model at more points, causing better overhang but making support harder to remove.
    def = defs.add("support_tree_branch_distance", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Distance"
    def->tooltip = L("How far apart the branches need to be when they touch the model. "
                     "Making this distance small will cause the tree support to touch the model at more points, "
                     "causing better overhang but making support harder to remove.");
    def->init_fn = init_with(1.);

    def = defs.add("support_tree_top_rate", typeid(Percentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Density"
    def->tooltip = L("Adjusts the density of the support structure used to generate the tips of the branches. "
                     "A higher value results in better overhangs but the supports are harder to remove, "
                     "thus it is recommended to enable top support interfaces instead of a high branch density value "
                     "if dense interfaces are needed.");
    def->units = {L("%")};
    def->min = 5;
    def->max_literal = 35;
    def->init_fn = init_with(Percentage{15.});

    def = defs.add("temperature", typeid(int));
    def->location = Filament;
    def->label = L("Nozzle other layers");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_NozzleTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Nozzle temperature for layers after the first one. Set this to zero to disable "
                     "temperature control commands in the output G-code.");
    def->units = {L("°C")};
    def->full_label = L("Nozzle temperature");
    def->min = 0;
    def->max = max_temp;
    def->init_fn = init_with(200);

    def = defs.add("thick_bridges", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Thick bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, bridges are more reliable, can bridge longer distances, but may look worse. "
                     "If disabled, bridges look better but are reliable just for shorter bridged distances.");
    def->init_fn = init_with(true);

    def = defs.add("thin_walls", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Detect thin walls");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Detect single-width walls (parts where two extrusions don't fit and we need "
                   "to collapse them into a single trace).");
    def->init_fn = init_with(true);

    def = defs.add("toolchange_gcode", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Tool change G-code");
    def->tooltip = L("This custom code is inserted before every toolchange. Placeholder variables for all PiggieSlicer settings "
                     "as well as {toolchange_z}, {previous_extruder} and {next_extruder} can be used. When a tool-changing command "
                     "which changes to the correct extruder is included (such as T{next_extruder}), PiggieSlicer will emit no other such command. "
                     "It is therefore possible to script custom behaviour both before and after the toolchange.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->init_fn = init_with("");

    def = defs.add("top_infill_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Top solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for top surfaces. "
                   "You may want to use thinner extrudates to fill all narrow regions and get a smoother finish. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("top_solid_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Top solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Speed for printing top solid layers (it only applies to the uppermost "
                   "external layers and not to their internal solid layers). You may want "
                   "to slow down this to get a nicer surface finish. This can be expressed "
                   "as a percentage (for example: 80%) over the solid infill speed above. "
                   "Set to zero for auto.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{15.});
    def->ratio_over = "solid_infill_speed";

    def = defs.add("top_solid_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Top solid layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_TopBottomShells;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 0;
    // def->row_group = L("Solid layers"); Temporary removed, row_group are not supported in PrintTool
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Number of solid layers to generate on top surfaces.");
    def->min = 0;
    def->init_fn = init_with(3);

    def = defs.add("top_solid_min_thickness", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Minimum top shell thickness");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_TopBottomShells;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 1;
    // def->row_group = L("Minimum shell thickness"); Temporary removed, row_group are not supported in PrintTool
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The number of top solid layers is increased above top_solid_layers if necessary to satisfy "
    				 "minimum thickness of top shell."
    				 " This is useful to prevent pillowing effect when printing with variable layer height.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("travel_speed", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool };
    def->label = L("Travel");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_Travels;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for travel moves (jumps between distant extrusion points).");
    def->units = {L("mm/s")};
    def->aliases = { "travel_feed_rate" };
    def->min = 1;
    def->init_fn = init_with(130.);

    def = defs.add("travel_speed_z", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool };
    def->label = L("Z travel");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_Travels;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for movements along the Z axis.\nWhen set to zero, the value "
                     "is ignored and regular travel speed is used instead.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("use_firmware_retraction", typeid(bool));
    def->location = Printer;
    def->label = L("Use firmware retraction");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This setting uses G10 and G11 commands to have the firmware "
                   "handle the retraction. Note that this has to be supported by firmware.");
    def->init_fn = init_with(false);

    def = defs.add("stuck_filament_detection", typeid(bool));
    def->location = Printer;
    def->label = L("Supports stuck filament monitoring configuration");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable, if the firmware supports configuring the stuck filament detection via the M591 gcode.");
    def->init_fn = init_with(false);

    def = defs.add("use_relative_e_distances", typeid(bool));
    def->location = Printer;
    def->label = L("Use relative E distances");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If your firmware requires relative E values, check this, "
                   "otherwise leave it unchecked. Most firmwares use absolute values.");
    def->init_fn = init_with(false);

    def = defs.add("use_volumetric_e", typeid(bool));
    def->location = Printer;
    def->label = L("Use volumetric E");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This experimental setting uses outputs the E values in cubic millimeters "
                   "instead of linear millimeters. If your firmware doesn't already know "
                   "filament diameter(s), you can put commands like 'M200 D[filament_diameter_0] T0' "
                   "in your start G-code in order to turn volumetric mode on and use the filament "
                   "diameter associated to the filament selected in Slic3r. This is only supported "
                   "in recent Marlin.");
    def->init_fn = init_with(false);

    def = defs.add("variable_layer_height", typeid(bool));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_General;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Enable variable layer height feature");
    def->tooltip = L("Some printers or printer setups may have difficulties printing "
                   "with a variable layer height. Enabled by default.");
    def->init_fn = init_with(true);

    def = defs.add("prefer_clockwise_movements", typeid(bool));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_General;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Prefer clockwise movements");
    def->tooltip = L("This setting makes the printer print loops clockwise instead of counterclockwise.");
    def->init_fn = init_with(false);

    def = defs.add("wipe", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Wipe while retracting");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This flag will move the nozzle while retracting to minimize the possible blob "
                   "on leaky extruders.");
    def->init_fn = init_with(false);

    def = defs.add("wipe_distance", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Wipe distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Describe how long the nozzle will move along the last path when retracting. Depending on how long "
                     "the wipe operation lasts, how fast and long the extruder/filament retraction settings are, a "
                     "retraction move may be needed to retract the remaining filament. Setting a value in the retract "
                     "amount before wipe setting below will perform any excess retraction before the wipe, else it will "
                     "be performed after.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("wipe_before_external_loop", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Wipe before external loop");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("To minimize visibility of potential overextrusion at the start of an external perimeter when "
                     "printing with Outer/Inner or Inner/Outer/Inner wall print order, the de-retraction is performed "
                     "slightly on the inside from the start of the external perimeter. That way any potential over "
                     "extrusion is hidden from the outside surface. This is useful when printing with Outer/Inner or "
                     "Inner/Outer/Inner wall print order as in these modes it is more likely an external perimeter is "
                     "printed immediately after a de-retraction move.");
    def->init_fn = init_with(false);

    def = defs.add("wipe_on_loops", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Wipe on loops");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("To minimize the visibility of the seam in a closed loop extrusion, a small inward movement is "
                     "executed before the extruder leaves the loop.");
    def->init_fn = init_with(false);

    def = defs.add("wipe_inward", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Wipe inward");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Applies only to external walls, including hole boundaries. Moves the hot nozzle toward printed "
                     "inner walls during wiping to reduce reheating of freshly printed plastic and seam marks. Especially "
                     "useful at layer heights below 0.1 mm, where wipe marks are more visible. Uses the regular wipe if "
                     "no adjacent inner wall is already printed (single-wall areas or Outer/Inner wall order), or if no "
                     "supported inward path can be found, for example at tight corners or seam gaps.");
    def->init_fn = init_with(false);

    def = defs.add("wipe_inward_distance", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Wipe inward distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->units = {L("mm"), L("%")};
    def->tooltip = L("The distance the wipe path is shifted away from the external perimeter, specified in millimeters or "
                     "as a percentage of the actual outer-wall extrusion width. For example, 50% shifts the path by half "
                     "of the outer-wall width. The effective offset is limited by both the actual outer-wall width and "
                     "the available spacing to the adjacent wall, so values above 100% or an equivalent absolute distance "
                     "have no additional effect. Set to 0 to disable the offset.");
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(FloatOrPercentage(Percentage{50.}));

    def = defs.add("wipe_tower", typeid(bool));
    def->location = Print;
    def->label = L("Enable");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Multi material printers may need to prime or purge extruders on tool changes. "
                   "Extrude the excess material into the wipe tower.");
    def->init_fn = init_with(false);

    def = defs.add("wiping_volumes_matrix", typeid(std::vector<double>));
    def->location = Project;
    def->category = ConfigItemDef::Category::Hidden;
    def->label = L("Purging volumes - matrix");
    def->tooltip = L("This matrix describes volumes (in cubic milimetres) required to purge the"
                     " new filament on the wipe tower for any given pair of tools.");
    def->init_fn = init_with(std::vector
                {   0., 140., 140., 140., 140.,
                  140.,   0., 140., 140., 140.,
                  140., 140.,   0., 140., 140.,
                  140., 140., 140.,   0., 140.,
                  140., 140., 140., 140.,   0. });

    def = defs.add("wiping_volumes_use_custom_matrix", typeid(bool));
    def->location = Project;
    def->category = ConfigItemDef::Category::Hidden;
    def->label = "";
    def->tooltip = "";
    def->init_fn = init_with(false);

    def = defs.add("wipe_tower_width", typeid(double));
    def->location = Print;
    def->label = L("Width");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Width of a wipe tower");
    def->units = {L("mm")};
    def->init_fn = init_with(60.);
    def->min = 1.0;

    // Ported from OrcaSlicer (prime_volume, PrintConfig.cpp): used instead of the purging
    // volumes when the printer does not purge in the prime tower (purge_in_prime_tower).
    def = defs.add("prime_volume", typeid(double));
    def->location = Print;
    def->label = L("Prime volume");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the volume of material to prime the extruder with on the tower.");
    def->units = {L("mm³")};
    def->init_fn = init_with(45.);
    def->min = 1.0;

    def = defs.add("wipe_tower_brim_width", typeid(double));
    def->location = Print;
    def->label = L("Wipe tower brim width");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Wipe tower brim width");
    def->units = {L("mm")};
    def->min = 0.;
    def->init_fn = init_with(2.);

    def = defs.add("wipe_tower_cone_angle", typeid(double));
    def->location = Print;
    def->label = L("Stabilization cone apex angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Angle at the apex of the cone that is used to stabilize the wipe tower. "
                     "Larger angle means wider base.");
    def->units = {L("°")};
    def->min = 0.;
    def->max = 90.;
    def->init_fn = init_with(0.);

    def = defs.add("wipe_tower_extra_spacing", typeid(Percentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{Tool};
    def->label = L("Wipe tower purge lines spacing");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Spacing of purge lines on the wipe tower.");
    def->units = {L("%")};
    def->min = 100.;
    def->max = 300.;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("wipe_tower_extra_flow", typeid(Percentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{Tool};
    def->label = L("Extra flow for purging");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Extra flow used for the purging lines on the wipe tower. This makes the purging lines thicker or narrower "
                     "than they normally would be. The spacing is adjusted automatically.");
    def->units = {L("%")};
    def->min = 100.;
    def->max = 300.;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("wipe_into_infill", typeid(bool));
    def->location = Volume;
    def->category = ConfigItemDef::Category::Volume_WipeOptions;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Wipe into this object's infill");
    def->tooltip = L("Purging after toolchange will be done inside this object's infills. "
                     "This lowers the amount of waste but may result in longer print time "
                     " due to additional travel moves.");
    def->init_fn = init_with(false);

    def = defs.add("wipe_into_objects", typeid(bool));
    def->location = Object;
    def->category = ConfigItemDef::Category::Volume_WipeOptions;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Wipe into this object");
    def->tooltip = L("Object will be used to purge the nozzle after a toolchange to save material "
                     "that would otherwise end up in the wipe tower and decrease print time. "
                     "Colours of the objects will be mixed as a result.");
    def->init_fn = init_with(false);

    def = defs.add("wipe_tower_bridging", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool };
    def->label = L("Maximal bridging distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximal distance between supports on sparse infill sections.");
    def->units = {L("mm")};
    def->init_fn = init_with(10.);

    def = defs.add("wipe_tower_extruder", typeid(int));
    def->location = Print;
    def->label = L("Wipe tower extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing perimeter of the wipe tower. "
                     "Set to 0 to use the one that is available (non-soluble would be preferred).");
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("make_overhang_printable", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Make overhangs printable");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 29;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Modify the geometry to print overhangs without support material.");
    def->init_fn = init_with(false);

    def = defs.add("make_overhang_printable_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Make overhangs printable - Maximum angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 30;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum angle of overhangs to allow after making more steep overhangs printable.90° will not change "
                     "the model at all and allow any overhang, while 0 will replace all overhangs with conical material.");
    def->units = {L("°")};
    def->min = 0.;
    def->max = 90.;
    def->init_fn = init_with(55.);

    def = defs.add("make_overhang_printable_hole_size", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Make overhangs printable - Hole area");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 31;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum area of a hole in the base of the model before it's filled by conical material. A value of "
                     "0 will fill all the holes in the model base.");
    def->units = {L("mm²")};
    def->min = 0.;
    def->init_fn = init_with(0.);

    def = defs.add("precise_outer_wall", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Precise wall");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 23;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Improve shell precision by adjusting outer wall spacing. This also improves layer consistency. "
                     "NOTE: This option will be ignored for outer-inner or inner-outer-inner wall sequences.");
    def->init_fn = init_with(true);

    def = defs.add("precise_z_height", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Precise Z height");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 24;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable this to get precise Z height of object after slicing. It will get the precise object height "
                     "by fine-tuning the layer heights of the last few layers. Note that this is an experimental "
                     "parameter.");
    def->init_fn = init_with(false);

    def = defs.add("hole_to_polyhole", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Convert holes to polyholes");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 25;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Search for almost-circular holes that span more than one layer and convert the geometry to "
                     "polyholes. Use the nozzle size and the (biggest) diameter to compute the polyhole. See "
                     "http://hydraraptor.blogspot.com/2011/02/polyholes.html");
    def->init_fn = init_with(false);

    def = defs.add("hole_to_polyhole_threshold", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Polyhole detection margin");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 26;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->units = {L("mm"), L("%")};
    def->tooltip = L("Maximum defection of a point to the estimated radius of the circle. As cylinders are often exported "
                     "as triangles of varying size, points may not be on the circle circumference. This setting allows "
                     "you some leeway to broaden the detection. In mm or in % of the radius.");
    def->init_fn = init_with(FloatOrPercentage(0.01));

    def = defs.add("hole_to_polyhole_twisted", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Polyhole twist");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 27;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Rotate the polyhole every layer.");
    def->init_fn = init_with(true);

    def = defs.add("hole_to_polyhole_max_edges", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Maximum Polyhole edge count");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 28;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Maximum number of polyhole edges This setting limits the amount of edges a polyhole can have");
    def->min = 3;
    def->init_fn = init_with(50);

    def = defs.add("xy_size_compensation", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("XY Size Compensation");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The object will be grown/shrunk in the XY plane by the configured value "
                   "(negative = inwards, positive = outwards). This might be useful "
                   "for fine-tuning hole sizes.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("z_offset", typeid(double));
    def->location = Printer;
    def->label = L("Z offset");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_SizeClearances;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This value will be added (or subtracted) from all the Z coordinates "
                   "in the output G-code. It is used to compensate for bad Z endstop position: "
                   "for example, if your endstop zero actually leaves the nozzle 0.3mm far "
                   "from the print bed, set this to -0.3 (or fix your endstop).");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("perimeter_generator", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Perimeter generator");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_PerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Classic perimeter generator produces perimeters with constant extrusion width and for "
                      "very thin areas is used gap-fill. "
                      "Arachne engine produces perimeters with variable extrusion width. "
                      "This setting also affects the Concentric infill.");

    def->init_fn = init_with(
        PerimeterGeneratorType::Arachne,
        {{int(PerimeterGeneratorType::Classic), "classic", L("Classic")},
         {int(PerimeterGeneratorType::Arachne), "arachne", L("Arachne")}}
    );

    def = defs.add("wall_transition_length", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Perimeter transition length");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip  = L("When transitioning between different numbers of perimeters as the part becomes "
                       "thinner, a certain amount of space is allotted to split or join the perimeter segments. "
                       "If expressed as a percentage (for example 100%), it will be computed based on the nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("wall_transition_filter_deviation", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Perimeter transitioning filter margin");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip  = L("Prevent transitioning back and forth between one extra perimeter and one less. This "
                       "margin extends the range of extrusion widths which follow to [Minimum perimeter width "
                       "- margin, 2 * Minimum perimeter width + margin]. Increasing this margin "
                       "reduces the number of transitions, which reduces the number of extrusion "
                       "starts/stops and travel time. However, large extrusion width variation can lead to "
                       "under- or overextrusion problems. "
                       "If expressed as a percentage (for example 25%), it will be computed based on the nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{25.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("wall_transition_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Perimeter transitioning threshold angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("When to create transitions between even and odd numbers of perimeters. A wedge shape with"
                       " an angle greater than this setting will not have transitions and no perimeters will be "
                       "printed in the center to fill the remaining space. Reducing this setting reduces "
                       "the number and length of these center perimeters, but may leave gaps or overextrude.");
    def->units = {L("°")};
    def->min = 1.;
    def->max = 59.;
    def->init_fn = init_with(10.);

    def = defs.add("wall_distribution_count", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Perimeter distribution count");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = L("The number of perimeters, counted from the center, over which the variation needs to be "
                       "spread. Lower values mean that the outer perimeters don't change in width.");
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("min_feature_size", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Minimum feature size");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip  = L("Minimum thickness of thin features. Model features that are thinner than this value will "
                       "not be printed, while features thicker than the Minimum feature size will be widened to "
                       "the Minimum perimeter width. "
                       "If expressed as a percentage (for example 25%), it will be computed based on the biggest nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{25.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("min_bead_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Minimum perimeter width");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip  = L("Width of the perimeter that will replace thin features (according to the Minimum feature size) "
                       "of the model. If the Minimum perimeter width is thinner than the thickness of the feature,"
                       " the perimeter will become as thick as the feature itself. "
                       "If expressed as a percentage (for example 85%), it will be computed based on the nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{85.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("initial_layer_min_bead_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("First layer minimum wall width");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("The minimum wall width that should be used for the first layer is recommended to be set to the same "
                     "size as the nozzle. This adjustment is expected to enhance adhesion.");
    def->units = {L("mm"), L("%")};
    def->min = 0;
    // a percentage of the (smallest) nozzle diameter: resolved in Arachne::WallToolPaths
    def->init_fn = init_with(FloatOrPercentage(Percentage{85.}));

    def = defs.add("min_length_factor", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Minimum wall length");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Adjust this value to prevent short, unclosed walls from being printed, which could increase print "
                     "time. The value is a multiple of the wall width. Higher values remove more and longer walls. NOTE: "
                     "Bottom and top surfaces will not be affected by this value to prevent visual gaps on the outside of "
                     "the model. Adjust 'One wall threshold' in the 'Walls and surfaces' settings below to adjust the "
                     "sensitivity of what is considered a top-surface. 'One wall threshold' is only visible if this "
                     "setting is set above the default value of 0.5, or if single-wall top surfaces is enabled.");
    def->min = 0;
    def->max = 25;
    def->init_fn = init_with(0.5);

    def = defs.add("wall_maximum_resolution", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Maximum wall resolution");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This value determines the smallest wall line segment length in mm. The smaller you set this value, "
                     "the more accurate and precise the walls will be.");
    def->units = {L("mm")};
    def->min = 0.005;
    def->max = 0.5;
    def->init_fn = init_with(0.5);

    def = defs.add("wall_maximum_deviation", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Maximum wall deviation");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The maximum deviation allowed when reducing the resolution for the 'Maximum wall resolution' "
                     "setting. If you increase this, the print will be less accurate, but the G-code will be smaller. "
                     "'Maximum wall deviation' limits 'Maximum wall resolution', so if the two conflict, 'Maximum wall "
                     "deviation' takes precedence.");
    def->units = {L("mm")};
    def->min = 0.005;
    def->max = 0.05;
    def->init_fn = init_with(0.025);

    def = defs.add("idle_temperature", typeid(std::optional<int>));
    def->location = Filament;
    def->label = L("Idle temperature");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_NozzleTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Nozzle temperature when the tool is currently not used in multi-tool setups."
                     "This is only used when 'Ooze prevention' is active in Print Settings.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = max_temp;
    def->init_fn = init_with(std::optional<int>());

    def = defs.add("custom_parameters_print", typeid(std::string));
    def->location = Print;
    def->category = ConfigItemDef::Category::Print_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Print_CustomGCode;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Custom print parameters");
    // FIXME: Translating the tooltip like this will likely not work.
    const std::string custom_parameter_tooltip_templ =
        L("JSON-encoded string defining extra parameters, which can later be expanded in Custom G-code macro language. "
          "Each of the parameters is prepended by %1% prefix before it is passed into the parser. "
          "For example, defining '{\"my_key\": \"my_value\"}' allows to use '%1%_my_key'.\n\n"
          "The JSON must be single level (no arrays and objects). All value types are allowed, but nulls "
          "will be rejected by the parser."
    );
    def->tooltip = (boost::format(custom_parameter_tooltip_templ) % "custom_parameter_print").str();
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->init_fn = init_with("");

    def = defs.add("custom_parameters_printer", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Custom printer parameters");
    def->tooltip = (boost::format(custom_parameter_tooltip_templ) % "custom_parameter_printer").str();;
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("");

    def = defs.add("custom_parameters_filament", typeid(std::string));
    def->location = Filament;
    def->order = 2;
    def->category = ConfigItemDef::Category::Filament_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Filament_CustomGCode;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Custom filament parameters");
    def->tooltip = (boost::format(custom_parameter_tooltip_templ) % "custom_parameter_filament").str();
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("");

    def = defs.add("default_tool_print", typeid(std::string));
    def->location = Print;
    def->label = L("Default Tool Print Preset");
    def->category = ConfigItemDef::Category::Hidden;
    def->tooltip = L("Name or ID of tool print preset to use as default when this print preset is selected.");
    def->init_fn = init_with("");

    // Orca's sidebar layout and modes of the print settings
    apply_orca_layout(defs);
}

} // namespace Slic3r::Domain

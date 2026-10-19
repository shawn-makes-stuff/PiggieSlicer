#include "Slic3r/Domain/SettingsToggles.hpp"

#include <initializer_list>
#include <optional>
#include <type_traits>

#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/InfillPatternTraits.hpp"

namespace Slic3r::Domain {

namespace {

// Reads the settings whatever way they are stored (a per tool setting may be a vector: its first value).
class Values
{
public:
    explicit Values(const ConfigValueGetter& value_of) : m_value_of(value_of) {}

    // bool, int, double, percentages: as a number; a missing setting is `missing`
    double num(const std::string& key, double missing = 0.) const
    {
        const ConfigValue* value = m_value_of(key);
        if (value == nullptr)
            return missing;
        return value->visit([missing](const auto& v) -> double { return number(v, missing); });
    }
    bool on(const std::string& key) const { return num(key) != 0.; }

    template<typename T> std::optional<T> enumeration(const std::string& key) const
    {
        const ConfigValue* value = m_value_of(key);
        if (value == nullptr)
            return {};
        if (value->holds_alternative<EnumWrapper>())
            return value->get<T>();
        if (value->holds_alternative<EnumVectorWrapper>())
            if (const std::vector<T> values = value->get<std::vector<T>>(); !values.empty())
                return values.front();
        return {};
    }

    std::string text(const std::string& key) const
    {
        const ConfigValue* value = m_value_of(key);
        if (value != nullptr && value->holds_alternative<std::string>())
            return value->get<std::string>();
        return {};
    }

private:
    template<typename V> static double number(const V& v, double missing)
    {
        if constexpr (std::is_same_v<V, bool> || std::is_same_v<V, int> || std::is_same_v<V, double>)
            return double(v);
        else if constexpr (std::is_same_v<V, std::optional<int>>)
            return v ? double(*v) : missing;
        else if constexpr (std::is_same_v<V, Percentage>)
            return v.value;
        else if constexpr (std::is_same_v<V, FloatOrPercentage>)
            return v.is_zero() ? 0. : v.get_abs_value(100.);
        else if constexpr (std::is_same_v<V, std::vector<bool>> || std::is_same_v<V, std::vector<int>> ||
                           std::is_same_v<V, std::vector<double>> || std::is_same_v<V, std::vector<Percentage>> ||
                           std::is_same_v<V, std::vector<FloatOrPercentage>> || std::is_same_v<V, std::vector<std::optional<int>>>)
            return v.empty() ? missing : number(typename V::value_type(v.front()), missing);
        else
            return missing;
    }

    const ConfigValueGetter& m_value_of;
};

// Orca: the infill patterns with multiline support (fill_multiline)
bool supports_multiline(InfillPattern p)
{
    return
        p == InfillPattern::ipGyroid || p == InfillPattern::ipGrid || p == InfillPattern::ipRectilinear ||
        p == InfillPattern::ipTpmsD || p == InfillPattern::ipTpmsFK || p == InfillPattern::ipCrossHatch ||
        p == InfillPattern::ipHoneycomb || p == InfillPattern::ipLateralLattice || p == InfillPattern::ipLateralHoneycomb ||
        p == InfillPattern::ipConcentric || p == InfillPattern::ipCubic || p == InfillPattern::ipStars ||
        p == InfillPattern::ipAlignedRectilinear || p == InfillPattern::ipLightning || p == InfillPattern::ip3DHoneycomb ||
        p == InfillPattern::ipAdaptiveCubic || p == InfillPattern::ipSupportCubic || p == InfillPattern::ipTriangles ||
        p == InfillPattern::ipQuarterCubic || p == InfillPattern::ipArchimedeanChords ||
        p == InfillPattern::ipHilbertCurve || p == InfillPattern::ipOctagramSpiral;
}

class Toggles
{
public:
    explicit Toggles(SettingsToggles& out) : m_out(out) {}
    // Orca toggle_field: greyed out unless `enabled`
    void field(std::initializer_list<const char*> keys, bool enabled)
    {
        if (!enabled)
            for (const char* key : keys)
                m_out.disabled.insert(key);
    }
    // Orca toggle_line: hidden unless `shown`
    void line(std::initializer_list<const char*> keys, bool shown)
    {
        if (!shown)
            for (const char* key : keys)
                m_out.hidden.insert(key);
    }

private:
    SettingsToggles& m_out;
};

} // namespace

SettingsToggles print_settings_toggles(const ConfigValueGetter& value_of, const PrinterKind& printer)
{
    SettingsToggles result;
    const Values    v{value_of};
    Toggles         t{result};

    // the printer: multi-material settings with more than one filament, multi-nozzle ones with more than one nozzle
    const bool multi_material = printer.filaments > 1;
    const bool multi_nozzle   = printer.nozzles > 1;
    t.line({"wipe_tower", "wipe_tower_width", "wipe_tower_brim_width",
            "wipe_tower_cone_angle", "wipe_tower_extra_spacing", "wipe_tower_extra_flow", "wipe_tower_bridging",
            "wipe_tower_no_sparse_layers", "wipe_tower_acceleration", "wipe_tower_extruder", "wipe_tower_wall_type",
            "wipe_tower_extra_rib_length", "wipe_tower_rib_width", "wipe_tower_fillet_wall", "wipe_tower_max_purge_speed",
            "prime_volume", "wipe_into_infill",
            "wipe_into_objects", "toolchange_ordering", "toolchange_cyclic_order", "toolchange_cyclic_first_layer",
            "mmu_segmented_region_max_width", "mmu_segmented_region_interlocking_depth", "interlocking_beam", "interlocking_beam_width",
            "interlocking_orientation", "interlocking_beam_layer_count", "interlocking_depth", "interlocking_boundary_avoidance",
            "perimeter_extruder", "infill_extruder", "solid_infill_extruder", "support_material_extruder",
            "support_material_interface_extruder", "single_extruder_multi_material_priming", "bed_temperature_extruder",
            "interface_shells"},
           multi_material);
    // Orca: ooze prevention is for the idle nozzles (off with a single extruder multi material setup)
    t.line({"ooze_prevention", "standby_temperature_delta", "preheat_time", "preheat_steps"},
           multi_nozzle && !printer.single_extruder_multi_material);

    // extrusion rate smoothing (Orca: arc fitting off with it, its segment length with it)
    const bool have_rate_slope = v.num("max_volumetric_extrusion_rate_slope_positive") > 0. ||
                                 v.num("max_volumetric_extrusion_rate_slope_negative") > 0.;
    const bool arc_fitting = v.enumeration<ArcFittingType>("arc_fitting").value_or(ArcFittingType::Disabled) != ArcFittingType::Disabled;
    t.field({"arc_fitting"}, !have_rate_slope || arc_fitting);
    t.line({"max_volumetric_extrusion_rate_slope_segment_length", "extrusion_rate_smoothing_external_perimeter_only"}, have_rate_slope);

    // walls
    const bool have_perimeters = v.num("perimeters") > 0;
    t.field({"extra_perimeters", "extra_perimeters_on_overhangs", "ensure_vertical_shell_thickness", "thin_walls", "overhangs",
             "seam_position", "staggered_inner_seams", "wall_sequence", "external_perimeter_extrusion_width", "perimeter_speed",
             "small_perimeter_speed", "small_perimeter_threshold", "external_perimeter_speed", "enable_dynamic_overhang_speeds"},
            have_perimeters);

    // sparse infill
    const bool          have_infill = v.num("fill_density") > 0;
    const InfillPattern pattern     = v.enumeration<InfillPattern>("fill_pattern").value_or(InfillPattern::ipRectilinear);
    t.line({"fill_pattern", "automatic_infill_combination", "fill_multiline", "fill_angle", "infill_complete_top", "solid_infill_below_area",
            "infill_extruder", "infill_shift_step", "sparse_infill_rotate_template", "symmetric_infill_y_axis"},
           have_infill);
    t.field({"infill_every_layers", "solid_infill_every_layers"}, have_infill);
    // Orca: the concentric patterns have nothing for an anchor to attach to
    const bool have_infill_anchor = have_infill && pattern != InfillPattern::ipConcentric && pattern != InfillPattern::ipSpiralInset;
    t.line({"infill_anchor", "infill_anchor_max"}, have_infill_anchor);
    t.line({"automatic_infill_combination_max_layer_height"}, have_infill && v.on("automatic_infill_combination"));
    const bool multiline_pattern = supports_multiline(pattern);
    const bool have_tpms_infill   = have_infill && v.num("fill_density") < 100 && is_tpms_adaptive_pattern(pattern);
    const bool have_tpms_adaptive = have_tpms_infill &&
        v.enumeration<TpmsAdaptiveMode>("tpms_adaptive").value_or(TpmsAdaptiveMode::Disabled) != TpmsAdaptiveMode::Disabled;
    t.line({"tpms_adaptive"}, have_tpms_infill);
    t.line({"tpms_interior_density", "tpms_adaptive_gradient"}, have_tpms_adaptive);
    t.line({"gyroid_optimized"}, have_infill && pattern == InfillPattern::ipGyroid && !have_tpms_adaptive);
    if (have_infill) {
        t.field({"fill_multiline"}, multiline_pattern);
        const bool anchor_max = pattern != InfillPattern::ipLine;
        t.field({"infill_anchor_max"}, anchor_max);
        t.field({"infill_anchor"}, anchor_max && v.num("infill_anchor_max") > 0);
    }
    const bool cross_zag  = pattern == InfillPattern::ipCrossZag;
    const bool locked_zag = pattern == InfillPattern::ipLockedZag;
    t.line({"infill_shift_step"}, cross_zag || locked_zag);
    t.line({"skeleton_infill_density", "skin_infill_density", "infill_lock_depth", "skin_infill_depth", "skin_infill_line_width",
            "skeleton_infill_line_width"},
           locked_zag);
    t.line({"symmetric_infill_y_axis"}, pattern == InfillPattern::ipZigZag || cross_zag || locked_zag);
    t.line({"infill_complete_top"}, have_infill && pattern == InfillPattern::ip3DHoneycomb);

    // spiral vase, top / bottom shells
    const bool spiral_vase = v.on("spiral_vase");
    t.line({"spiral_mode_smooth", "spiral_starting_flow_ratio", "spiral_finishing_flow_ratio"}, spiral_vase);
    t.line({"spiral_mode_max_xy_smoothing"}, spiral_vase && v.on("spiral_mode_smooth"));
    const bool has_top_shell_layers = v.num("top_solid_layers") > 0 || (spiral_vase && v.num("bottom_solid_layers") > 1);
    const bool has_top_shell        = has_top_shell_layers && v.num("top_surface_density", 100.) > 0;
    const bool has_bottom_shell     = v.num("bottom_solid_layers") > 0;
    const bool has_solid_infill     = has_top_shell_layers || has_bottom_shell;
    t.line({"sparse_infill_smooth_factor"}, have_infill && is_smoothable_infill_pattern(pattern, int(v.num("fill_multiline", 1.))));
    t.field({"top_fill_pattern", "top_layer_direction", "top_infill_extrusion_width", "top_solid_infill_speed"}, has_top_shell);
    t.field({"bottom_fill_pattern", "bottom_surface_density", "bottom_layer_direction"}, has_bottom_shell);
    t.field({"top_surface_density"}, has_top_shell_layers);
    t.field({"infill_first", "solid_infill_extruder", "solid_infill_extrusion_width", "solid_infill_speed"}, has_solid_infill);
    t.line({"top_surface_expansion", "top_surface_expansion_margin", "top_surface_expansion_direction"}, has_top_shell);
    t.field({"top_surface_expansion_margin", "top_surface_expansion_direction"}, v.num("top_surface_expansion") > 0);
    const auto centered = [](std::optional<InfillPattern> p) {
        return p == InfillPattern::ipArchimedeanChords || p == InfillPattern::ipOctagramSpiral;
    };
    const auto top_pattern    = v.enumeration<InfillPattern>("top_fill_pattern");
    const auto bottom_pattern = v.enumeration<InfillPattern>("bottom_fill_pattern");
    t.line({"center_of_surface_pattern"}, (has_top_shell && centered(top_pattern)) || (has_bottom_shell && centered(bottom_pattern)));
    t.line({"separated_infills"}, is_separable_infill_pattern(pattern));
    const auto centered_fill = [&centered](std::optional<InfillPattern> p) {
        return centered(p) || p == InfillPattern::ipConcentric || p == InfillPattern::ipSpiralInset;
    };
    t.line({"top_surface_fill_order"}, has_top_shell && centered_fill(top_pattern));
    t.line({"bottom_surface_fill_order"}, has_bottom_shell && centered_fill(bottom_pattern));
    t.field({"fill_angle", "infill_extrusion_width", "gap_fill_enabled", "filter_out_gap_fill", "infill_overlap", "bridge_angle",
             "internal_bridge_angle", "relative_bridge_angle", "solid_infill_direction", "solid_infill_rotate_template",
             "internal_solid_infill_pattern", "infill_speed", "bridge_speed", "internal_bridge_speed"},
            have_infill || has_solid_infill);
    t.field({"top_solid_min_thickness"}, !spiral_vase && has_top_shell_layers);
    t.field({"bottom_solid_min_thickness"}, !spiral_vase && has_bottom_shell);
    t.field({"gap_fill_speed"}, have_perimeters);

    // accelerations, jerk / junction deviation
    t.field({"perimeter_acceleration", "infill_acceleration", "top_solid_infill_acceleration", "solid_infill_acceleration",
             "external_perimeter_acceleration", "bridge_acceleration", "first_layer_acceleration", "wipe_tower_acceleration",
             "initial_layer_travel_acceleration"},
            v.num("default_acceleration") > 0);
    const bool marlin = printer.gcode_flavor == GCodeFlavor::gcfMarlinFirmware;
    t.line({"default_junction_deviation"}, marlin);
    if (printer.junction_deviation) {
        t.field({"default_jerk"}, false);
        t.line({"outer_wall_jerk", "inner_wall_jerk", "initial_layer_jerk", "initial_layer_travel_jerk", "top_surface_jerk", "travel_jerk",
                "infill_jerk"},
               false);
    } else {
        t.field({"default_junction_deviation"}, false);
        t.field({"outer_wall_jerk", "inner_wall_jerk", "initial_layer_jerk", "initial_layer_travel_jerk", "top_surface_jerk", "travel_jerk",
                 "infill_jerk"},
                v.num("default_jerk") > 0);
    }
    const bool klipper = printer.gcode_flavor == GCodeFlavor::gcfKlipper;
    t.line({"accel_to_decel_enable", "accel_to_decel_factor"}, klipper);
    t.field({"accel_to_decel_factor"}, v.on("accel_to_decel_enable"));

    // skirt, brim
    const bool have_skirt = v.num("skirts") > 0;
    t.field({"skirt_height"}, have_skirt && v.enumeration<DraftShield>("draft_shield") != DraftShield::dsEnabled);
    t.line({"single_loop_draft_shield"}, have_skirt);
    t.field({"skirt_type", "min_skirt_length", "skirt_distance", "skirt_start_angle", "skirt_speed", "draft_shield"}, have_skirt);
    const BrimType brim_type = v.enumeration<BrimType>("brim_type").value_or(BrimType::NoBrim);
    const bool     have_brim = brim_type != BrimType::NoBrim;
    t.field({"brim_separation", "brim_use_efc_outline", "combine_brims", "brim_flow_ratio"}, have_brim);
    t.field({"brim_width"}, have_brim && brim_type != BrimType::AutoBrim);
    t.field({"perimeter_extruder"}, have_perimeters || have_brim);
    const bool brim_ears = brim_type == BrimType::BrimEars;
    t.field({"brim_ears_max_angle", "brim_ears_detection_length", "brim_ears_outer_only"}, v.num("brim_width") > 0);
    t.line({"brim_ears_max_angle", "brim_ears_detection_length", "brim_ears_outer_only"}, brim_ears);
    t.line({"elefant_foot_compensation_layers"},
           v.num("elefant_foot_compensation") > 0 || v.num("elefant_foot_layers_density", 100.) < 100.);

    // supports, raft
    const bool have_raft             = v.num("raft_layers") > 0;
    // 3.0: support_material is Off / Manual only (the painted enforcers) / On (the threshold too)
    const SupportMode support_mode   = v.enumeration<SupportMode>("support_material").value_or(SupportMode::None);
    const bool have_support_material = support_mode != SupportMode::None || have_raft;
    const bool have_support_auto     = support_mode == SupportMode::Everywhere;
    const bool have_support_interface =
        v.num("support_material_interface_layers") > 0 || v.num("support_material_bottom_interface_layers") > 0;
    const bool have_support_soluble = have_support_material && v.num("support_material_contact_distance", 1.) == 0.;
    const auto support_style        = v.enumeration<SupportMaterialStyle>("support_material_style");
    t.field({"support_material_style", "support_material_pattern", "support_material_with_sheath", "support_material_spacing",
             "support_material_angle", "support_material_interface_pattern", "support_material_interface_layers", "dont_support_bridges",
             "support_material_extrusion_width", "support_material_contact_distance", "support_material_xy_spacing",
             "support_material_buildplate_only", "support_expansion", "support_object_first_layer_gap"},
            have_support_material);
    t.field({"support_material_threshold"}, have_support_auto);
    t.field({"support_threshold_overlap"}, have_support_auto && v.num("support_material_threshold") == 0);
    t.field({"support_material_bottom_contact_distance"}, have_support_material && !have_support_soluble);
    t.field({"support_material_closing_radius"}, have_support_material && support_style == SupportMaterialStyle::smsSnug);
    const bool support_is_tree = have_support_material &&
        (support_style == SupportMaterialStyle::smsTree || support_style == SupportMaterialStyle::smsOrganic);
    // Orca: tree supports do not use the threshold overlap
    t.line({"support_threshold_overlap"}, !support_is_tree);
    const bool organic = support_style == SupportMaterialStyle::smsOrganic &&
                         (support_mode != SupportMode::None || v.num("support_material_enforce_layers") > 0);
    t.line({"support_tree_angle", "support_tree_angle_slow", "support_tree_branch_diameter", "support_tree_branch_diameter_angle",
            "support_tree_branch_diameter_double_wall", "support_tree_tip_diameter", "support_tree_branch_distance", "support_tree_top_rate"},
           organic);
    t.field({"support_material_bottom_interface_layers", "support_material_interface_spacing", "support_material_interface_extruder",
             "support_material_interface_speed", "support_material_interface_contact_loops", "support_bottom_interface_spacing"},
            have_support_material && have_support_interface);
    t.field({"support_material_synchronize_layers"}, have_support_soluble);
    t.field({"perimeter_extrusion_width"}, have_perimeters || have_skirt || have_brim);
    t.field({"support_material_extruder"}, have_support_material || have_skirt);
    t.field({"support_material_speed"}, have_support_material || have_brim || have_skirt);
    t.field({"small_support_perimeter_speed", "small_support_perimeter_threshold"}, support_mode != SupportMode::None);
    t.line({"raft_contact_distance"}, have_raft && !have_support_soluble);
    t.line({"raft_expansion", "first_layer_acceleration_over_raft", "first_layer_speed_over_raft"}, have_raft);
    // Orca: support ironing on a raft or a top interface
    const bool can_ironing_support = have_raft || (have_support_material && v.num("support_material_interface_layers") > 0);
    t.field({"support_ironing"}, can_ironing_support);
    const bool has_support_ironing = can_ironing_support && v.on("support_ironing");
    t.line({"support_ironing_pattern", "support_ironing_flow", "support_ironing_spacing"}, has_support_ironing);
    // Orca: a solid interface with support ironing
    t.field({"support_material_interface_spacing"}, have_support_material && have_support_interface && !has_support_ironing);

    // ironing
    const bool has_ironing = v.on("ironing");
    t.field({"ironing_type", "ironing_flowrate", "ironing_spacing"}, has_ironing);
    t.line({"ironing_pattern", "ironing_angle", "ironing_inset", "ironing_angle_fixed"}, has_ironing);
    t.field({"ironing_angle", "ironing_angle_fixed"},
            has_ironing && v.enumeration<InfillPattern>("ironing_pattern").value_or(InfillPattern::ipRectilinear) == InfillPattern::ipRectilinear);
    t.line({"ironing_speed"}, has_ironing || has_support_ironing);

    // sequential printing
    const bool sequential = v.on("complete_objects");
    t.field({"extruder_clearance_radius", "extruder_clearance_height"}, sequential);
    t.field({"print_order"}, !sequential);

    // multi material
    const bool ooze_prevention = v.on("ooze_prevention");
    t.line({"standby_temperature_delta", "preheat_time"}, ooze_prevention);
    t.line({"preheat_steps"}, ooze_prevention && v.num("preheat_steps") > 0);
    const bool have_wipe_tower = v.on("wipe_tower");
    t.line({"wipe_tower_width", "wipe_tower_brim_width", "wipe_tower_wall_type", "wipe_tower_max_purge_speed",
            "wipe_tower_extra_spacing", "wipe_tower_extra_flow", "wipe_tower_bridging", "wipe_tower_no_sparse_layers", "wipe_tower_acceleration",
            "wipe_tower_extruder", "single_extruder_multi_material_priming"},
           have_wipe_tower);
    // Orca: the cone angle with the cone wall, the rib settings with the rib wall, which squares the tower itself
    const auto wall_type = v.enumeration<WipeTowerWallType>("wipe_tower_wall_type").value_or(WipeTowerWallType::Rib);
    const bool rib_wall  = have_wipe_tower && wall_type == WipeTowerWallType::Rib;
    t.line({"wipe_tower_cone_angle"}, have_wipe_tower && wall_type == WipeTowerWallType::Cone);
    t.line({"wipe_tower_extra_rib_length", "wipe_tower_rib_width", "wipe_tower_fillet_wall"}, rib_wall);
    t.field({"wipe_tower_width"}, have_wipe_tower && !rib_wall);
    // Orca: the tower's prime volume unless the purge goes into it on a single extruder multi material printer
    t.line({"prime_volume"}, have_wipe_tower && (!printer.purge_in_prime_tower || !printer.single_extruder_multi_material));
    t.field({"wipe_into_infill", "wipe_into_objects"}, have_wipe_tower);
    const bool cyclic = v.enumeration<ToolChangeOrderingType>("toolchange_ordering") == ToolChangeOrderingType::Cyclic;
    t.line({"toolchange_cyclic_order", "toolchange_cyclic_first_layer"}, cyclic);
    t.field({"mmu_segmented_region_interlocking_depth"}, v.num("mmu_segmented_region_max_width") > 0);
    const bool beam_interlocking = v.on("interlocking_beam");
    t.line({"mmu_segmented_region_interlocking_depth"}, !beam_interlocking);
    t.line({"interlocking_beam_width", "interlocking_orientation", "interlocking_beam_layer_count", "interlocking_depth",
            "interlocking_boundary_avoidance"},
           beam_interlocking);

    // travel: Prusa's two ways of avoiding crossing exclude each other (one that is on stays editable, to turn it off)
    t.field({"avoid_crossing_curled_overhangs"}, !v.on("avoid_crossing_perimeters") || v.on("avoid_crossing_curled_overhangs"));
    t.field({"avoid_crossing_perimeters"}, !v.on("avoid_crossing_curled_overhangs") || v.on("avoid_crossing_perimeters"));
    t.line({"avoid_crossing_perimeters_max_detour"}, v.on("avoid_crossing_perimeters"));

    // flow ratios, overhang speeds
    t.line({"first_layer_flow_ratio", "outer_wall_flow_ratio", "inner_wall_flow_ratio", "overhang_flow_ratio", "sparse_infill_flow_ratio",
            "internal_solid_infill_flow_ratio", "gap_fill_flow_ratio", "support_flow_ratio", "support_interface_flow_ratio"},
           v.on("set_other_flow_ratios"));
    const bool overhang_speeds = v.on("enable_dynamic_overhang_speeds");
    t.line({"overhang_speed_0", "overhang_speed_1", "overhang_speed_2", "overhang_speed_3", "slowdown_for_curled_perimeters"}, overhang_speeds);

    // fuzzy skin
    const bool fuzzy_skin = v.enumeration<FuzzySkinType>("fuzzy_skin").value_or(FuzzySkinType::None) != FuzzySkinType::None;
    t.line({"fuzzy_skin_mode", "fuzzy_skin_noise_type", "fuzzy_skin_point_dist", "fuzzy_skin_thickness", "fuzzy_skin_first_layer"}, fuzzy_skin);
    const auto noise  = v.enumeration<FuzzySkinNoiseType>("fuzzy_skin_noise_type").value_or(FuzzySkinNoiseType::Classic);
    const bool ripple = noise == FuzzySkinNoiseType::Ripple;
    t.line({"fuzzy_skin_scale"}, fuzzy_skin && noise != FuzzySkinNoiseType::Classic && !ripple);
    t.line({"fuzzy_skin_octaves"}, fuzzy_skin && noise != FuzzySkinNoiseType::Classic && noise != FuzzySkinNoiseType::Voronoi && !ripple);
    t.line({"fuzzy_skin_persistence"}, fuzzy_skin && (noise == FuzzySkinNoiseType::Perlin || noise == FuzzySkinNoiseType::Billow));
    t.line({"fuzzy_skin_ripples_per_layer", "fuzzy_skin_ripple_offset", "fuzzy_skin_layers_between_ripple_offset"}, fuzzy_skin && ripple);

    // Arachne
    const bool arachne = v.enumeration<PerimeterGeneratorType>("perimeter_generator") == PerimeterGeneratorType::Arachne;
    t.line({"wall_transition_length", "wall_transition_filter_deviation", "wall_transition_angle", "min_feature_size", "min_length_factor",
            "min_bead_width", "wall_distribution_count", "initial_layer_min_bead_width", "wall_maximum_resolution", "wall_maximum_deviation"},
           arachne);
    t.field({"thin_walls"}, !arachne);

    // wipes
    t.field({"wipe_speed"}, !v.on("role_based_wipe_speed"));
    t.line({"wipe_inward_distance"}, v.on("wipe_inward"));

    // overhangs, holes, one wall surfaces
    t.line({"make_overhang_printable_angle", "make_overhang_printable_hole_size"}, v.on("make_overhang_printable"));
    t.line({"only_one_perimeter_first_layer"}, has_bottom_shell);
    t.line({"top_one_perimeter_type"}, has_top_shell_layers);
    const bool one_wall_top = v.enumeration<TopOnePerimeterType>("top_one_perimeter_type").value_or(TopOnePerimeterType::None) !=
                              TopOnePerimeterType::None;
    t.line({"min_width_top_surface"}, (has_top_shell_layers && one_wall_top) || (v.num("min_length_factor") > 0.5 && arachne));
    t.line({"hole_to_polyhole_threshold", "hole_to_polyhole_twisted", "hole_to_polyhole_max_edges"}, v.on("hole_to_polyhole"));
    const bool detect_overhangs  = v.on("overhangs");
    const bool overhang_reverse  = v.on("overhang_reverse");
    t.line({"unsupported_wall_last"}, detect_overhangs);
    t.line({"overhang_reverse"}, !spiral_vase);
    t.line({"overhang_reverse_internal_only"}, !spiral_vase && overhang_reverse);
    t.line({"overhang_reverse_threshold"}, detect_overhangs && !spiral_vase && overhang_reverse && !v.on("overhang_reverse_internal_only"));
    t.line({"small_area_infill_flow_compensation_model"}, v.on("small_area_infill_flow_compensation"));

    // scarf seams
    t.field({"scarf_seam_placement"}, !spiral_vase);
    const bool scarf = !spiral_vase &&
        v.enumeration<ScarfSeamPlacement>("scarf_seam_placement").value_or(ScarfSeamPlacement::nowhere) != ScarfSeamPlacement::nowhere;
    t.line({"scarf_seam_only_on_smooth", "scarf_seam_start_height", "scarf_seam_entire_loop", "scarf_seam_length",
            "scarf_seam_on_inner_perimeters", "scarf_joint_speed", "scarf_joint_flow_ratio"},
           scarf);
    t.field({"scarf_seam_length"}, !v.on("scarf_seam_entire_loop"));

    // pattern specific
    t.line({"lateral_lattice_angle_1", "lateral_lattice_angle_2"}, pattern == InfillPattern::ipLateralLattice);
    t.line({"lightning_overhang_angle", "lightning_prune_angle", "lightning_straightening_angle"}, pattern == InfillPattern::ipLightning);
    // Orca: no rotation for the adaptive and support cubic infills; a rotate template replaces the direction
    const bool adaptive = pattern == InfillPattern::ipAdaptiveCubic || pattern == InfillPattern::ipSupportCubic;
    t.field({"sparse_infill_rotate_template"}, !adaptive);
    t.field({"fill_angle"}, v.text("sparse_infill_rotate_template").empty() && !adaptive);
    t.field({"solid_infill_direction"}, v.text("solid_infill_rotate_template").empty());
    t.line({"infill_overhang_angle"}, pattern == InfillPattern::ipLateralHoneycomb);

    return result;
}

std::vector<std::pair<std::string, ConfigValue>> print_settings_fixes(const std::string& changed_key, const ConfigValueGetter& value_of)
{
    std::vector<std::pair<std::string, ConfigValue>> fixes;
    const Values v{value_of};
    // the setting's current value with another value of the same type
    const auto fix = [&](const std::string& key, const auto& value) {
        const ConfigValue* current = value_of(key);
        if (current == nullptr)
            return;
        ConfigValue fixed = *current;
        fixed.set(value);
        if (!(fixed == *current))
            fixes.emplace_back(key, std::move(fixed));
    };

    // Prusa: one of the two ways of avoiding crossing
    if (changed_key == "avoid_crossing_perimeters" && v.on("avoid_crossing_perimeters"))
        fix("avoid_crossing_curled_overhangs", false);
    if (changed_key == "avoid_crossing_curled_overhangs" && v.on("avoid_crossing_curled_overhangs"))
        fix("avoid_crossing_perimeters", false);
    // Orca: no arc fitting with the extrusion rate smoothing
    if ((changed_key == "max_volumetric_extrusion_rate_slope_positive" || changed_key == "max_volumetric_extrusion_rate_slope_negative") &&
        v.num(changed_key) > 0)
        fix("arc_fitting", std::string("disabled"));
    // Orca: a single line for the patterns without multiline support
    if (changed_key == "fill_pattern") {
        const InfillPattern p = v.enumeration<InfillPattern>("fill_pattern").value_or(InfillPattern::ipRectilinear);
        const bool multiline = supports_multiline(p);
        if (!multiline)
            fix("fill_multiline", 1);
    }
    // Orca: reversing the internal walls only works at any overhang
    if (changed_key == "overhang_reverse_internal_only" && v.on("overhang_reverse_internal_only"))
        fix("overhang_reverse_threshold", FloatOrPercentage(Percentage{0.}));
    // Orca / Prusa (the "adjust the settings for spiral vase" answer): one wall, no top layers, no infill, no supports
    if (changed_key == "spiral_vase" && v.on("spiral_vase")) {
        fix("perimeters", 1);
        fix("top_solid_layers", 0);
        fix("fill_density", Percentage{0.});
        fix("support_material", std::string("none"));
        fix("support_material_enforce_layers", 0);
        fix("thin_walls", false);
        fix("overhang_reverse", false);
    }
    return fixes;
}

} // namespace Slic3r::Domain

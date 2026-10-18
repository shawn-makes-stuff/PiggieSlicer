#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"
namespace Slic3r::Domain {

// This is an example of using Config infrastructure.

// Next, define all enums that should be used in the config.

enum class ArcFittingType {
    Disabled,
    EmitCenter
};
// Orca activate_chamber_temp_control, with 3.0's own behaviour as the default: Auto = 3.0 sets the
// nominal chamber temperature at the start (M141), Off = no chamber command (Orca: off), On = Orca's
// M191 to the highest chamber temperature of the used filaments before the start G-code (Orca: on)
enum class ChamberTemperatureControl
{
    Auto,
    Off,
    On,
};
// Orca OverhangFanThreshold: the overhang (share of the line width not on the layer below) from which
// the overhang fan runs; None = every external perimeter, Bridge = 95%
enum class OverhangFanThreshold
{
    None,
    Overhang10,
    Overhang25,
    Overhang50,
    Overhang75,
    Bridge,
};
// Orca RetractLiftEnforceType: on which surfaces a retraction lifts
enum class RetractLiftEnforce
{
    AllSurfaces,
    TopOnly,
    BottomOnly,
    TopAndBottom,
};
// Orca TopSurfaceExpansionDirection
enum class TopSurfaceExpansionDirection
{
    InwardAndOutward,
    Inward,
    Outward,
};
// Orca SurfaceFillOrder
// Orca: how the TPMS / Gyroid density is graded from the surface of the object to its center
enum class TpmsAdaptiveMode {
    Disabled,
    DistanceWarp,
    SmoothBlend,
    SteppedShells,
    Lobes,
    NormalZ,
    NormalY,
    NormalX,
    Count,
};
// Orca: how the adaptive TPMS density changes from the object surface to its deepest point
enum class TpmsAdaptiveGradient {
    Linear,
    Quadratic,
    Exponential,
};
// Orca wall_sequence: the order the walls of an island are printed in
enum class WallSequence {
    InnerOuter,
    OuterInner,
    InnerOuterInner,
};
enum class SurfaceFillOrder
{
    Default,
    Outward,
    Inward,
};
// Orca CenterOfSurfacePattern
enum class CenterOfSurfacePattern
{
    EachSurface,
    EachModel,
    EachAssembly,
};
// Orca CounterboreHoleBridgingOption
enum class CounterboreHoleBridging
{
    None,
    PartiallyBridge,
    SacrificialLayer,
};
// Orca EnableExtraBridgeLayer
enum class EnableExtraBridgeLayer
{
    Disabled,
    ExternalBridgeOnly,
    InternalBridgeOnly,
    ApplyToAll,
};
// Orca InternalBridgeFilter
enum class InternalBridgeFilter
{
    Disabled,
    Limited,
    NoFilter,
};
// Orca PrintOrder
enum class PrintOrder
{
    Default,
    AsObjectList,
    BestOfStrategies,
    Snake,
};
// Orca PowerLossRecoveryMode
enum class PowerLossRecoveryMode
{
    PrinterConfiguration,
    Enable,
    Disable,
};
// Orca TimelapseType
enum class TimelapseType
{
    Traditional,
    Smooth,
};
// Orca BedTempFormula: the bed temperature of the first printing filament or the highest of the used ones
enum class BedTemperatureFormula
{
    ByFirstFilament,
    ByHighestTemp,
};
enum class CoolingSlowdownLogicType
{
    UniformCooling,
    ConsistentSurface,
    Proportional,
};
enum class TopOnePerimeterType
{
    None,
    TopSurfaces,
    TopmostOnly,
    Count
};
enum class BrimType {
    NoBrim,
    OuterOnly,
    InnerOnly,
    OuterAndInner,
    // Orca: ears at the sharp corners
    BrimEars,
    // Orca: an outer brim of a width by the material, height and shape of the model
    AutoBrim,
};
// Orca SkirtType
enum class SkirtType {
    Combined,
    PerObject,
};
enum class EnsureVerticalShellThickness {
    Disabled,
    Partial,
    Enabled,
};
enum class InfillPattern {
    ipRectilinear, ipMonotonic, ipMonotonicLines, ipAlignedRectilinear, ipGrid, ipTriangles,
    ipStars, ipCubic, ipLine, ipConcentric, ipHoneycomb, ip3DHoneycomb, ipGyroid, ipHilbertCurve,
    ipArchimedeanChords, ipOctagramSpiral, ipAdaptiveCubic, ipSupportCubic, ipSupportBase,
    ipLightning, ipEnsuring, ipZigZag,
    // Orca: the narrow internal solid areas (detect_narrow_internal_solid_infill), not selectable
    ipConcentricInternal,
    // 3.0's 3D Honeycomb generator
    ip3DHoneycombClassic,
    // Orca
    ipQuarterCubic, ipLateralLattice, ipLateralHoneycomb, ipCrossHatch, ipSpiralInset, ipTpmsD, ipTpmsFK,
    ipCrossZag, ipLockedZag,
    ipCount,
};
enum class FuzzySkinType {
    None,
    External,
    All,
    // Orca: holes only, every wall
    Hole,
    AllWalls,
};

// Orca FuzzySkinMode: how the noise is applied (Arachne)
enum class FuzzySkinMode {
    Displacement,
    Extrusion,
    Combined,
};

// Orca NoiseType
enum class FuzzySkinNoiseType {
    Classic,
    Perlin,
    Billow,
    RidgedMulti,
    Voronoi,
    Ripple,
};

enum class LabelObjectsStyle {
    Disabled, Octoprint, Firmware
};
enum class IroningType {
    TopSurfaces,
    TopmostOnly,
    AllSolid,
    Count,
};
enum class MachineLimitsUsage {
    EmitToGCode,
    TimeEstimateOnly,
    Ignore,
    Count,
};
enum class SeamPosition {
    spRandom, spNearest, spAligned, spRear
};
enum class ScarfSeamPlacement {
    nowhere,
    countours,
    everywhere
};
enum class DraftShield {
    dsDisabled, dsLimited, dsEnabled
};
enum class SupportMaterialPattern {
    smpRectilinear, smpRectilinearGrid, smpHoneycomb,
};
enum class SupportMaterialInterfacePattern {
    smipAuto, smipRectilinear, smipConcentric,
    // Orca
    smipSpiralInset, smipRectilinearInterlaced, smipGrid,
};
enum class SupportMaterialStyle {
    smsGrid, smsSnug, smsTree, smsOrganic,
};
enum class PerimeterGeneratorType
{
    Classic, // Classic perimeter generator using Clipper offsets with constant extrusion width.
    Arachne  // Perimeter generator with variable extrusion width based on the paper  "A framework for
             // "adaptive width control of dense contour-parallel toolpaths in fused deposition modeling" ported from Cura.
};
enum class SupportMode
{
    None,
    EnforcersOnly,
    Everywhere,
};

enum class PressureAdvance
{
    Disabled,
    Enabled,
    AutomaticCalibration,
};

enum class ToolChangeOrderingType
{
    Optimized,
    Cyclic,
};

const ConfigDefinitions& get_defs_fdm();

} // namespace Slic3r::Domain

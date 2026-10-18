#include <algorithm>
#include <cmath>
#include <vector>
#include <cassert>
#include <cstddef>

#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/Polyline.hpp"
#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "Fill3DHoneycomb.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Fill/FillCornerSmoothing.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {

namespace BB = Biz::Algorithms::BoundingBox;

/*
Creates a contiguous sequence of points at a specified height that make
up a horizontal slice of the edges of a space filling truncated
octahedron tesselation. The octahedrons are oriented so that the
square faces are in the horizontal plane with edges parallel to the X
and Y axes.

Credits: David Eccles (gringer).
*/

// Generate an array of points that are in the same direction as the
// basic printing line (i.e. Y points for columns, X points for rows)
// Note: a negative offset only causes a change in the perpendicular
// direction
static std::vector<double> colinearPoints(const double offset, const size_t baseLocation, size_t gridLength)
{
    const double offset2 = std::abs(offset / double(2.));
    std::vector<double> points;
    points.push_back(baseLocation - offset2);
    for (size_t i = 0; i < gridLength; ++i) {
        points.push_back(baseLocation + i + offset2);
        points.push_back(baseLocation + i + 1 - offset2);
    }
    points.push_back(baseLocation + gridLength + offset2);
    return points;
}

// Generate an array of points for the dimension that is perpendicular to
// the basic printing line (i.e. X points for columns, Y points for rows)
static std::vector<double> perpendPoints(const double offset, const size_t baseLocation, size_t gridLength)
{
    double offset2 = offset / double(2.);
    coord_t  side    = 2 * (baseLocation & 1) - 1;
    std::vector<double> points;
    points.push_back(baseLocation - offset2 * side);
    for (size_t i = 0; i < gridLength; ++i) {
        side = 2*((i+baseLocation) & 1) - 1;
        points.push_back(baseLocation + offset2 * side);
        points.push_back(baseLocation + offset2 * side);
    }
    points.push_back(baseLocation - offset2 * side);
    return points;
}

// Trims an array of points to specified rectangular limits. Point
// components that are outside these limits are set to the limits.
static inline void trim(Pointfs &pts, double minX, double minY, double maxX, double maxY)
{
    for (Vec2d &pt : pts) {
        pt.x() = std::clamp(pt.x(), minX, maxX);
        pt.y() = std::clamp(pt.y(), minY, maxY);
    }
}

static inline Pointfs zip(const std::vector<double> &x, const std::vector<double> &y)
{
    assert(x.size() == y.size());
    Pointfs out;
    out.reserve(x.size());
    for (size_t i = 0; i < x.size(); ++ i)
        out.push_back(Vec2d(x[i], y[i]));
    return out;
}

// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron with edge length 1.
// curveType specifies which lines to print, 1 for vertical lines
// (columns), 2 for horizontal lines (rows), and 3 for both.
static std::vector<Pointfs> makeNormalisedGrid(double z, size_t gridWidth, size_t gridHeight, size_t curveType)
{
    // offset required to create a regular octagram
    double octagramGap = double(0.5);
    
    // sawtooth wave function for range f($z) = [-$octagramGap .. $octagramGap]
    double a = std::sqrt(double(2.));  // period
    double wave = fabs(fmod(z, a) - a/2.)/a*4. - 1.;
    double offset = wave * octagramGap;
    
    std::vector<Pointfs> points;
    if ((curveType & 1) != 0) {
        for (size_t x = 0; x <= gridWidth; ++x) {
            points.push_back(Pointfs());
            Pointfs &newPoints = points.back();
            newPoints = zip(
                perpendPoints(offset, x, gridHeight), 
                colinearPoints(offset, 0, gridHeight));
            // trim points to grid edges
            trim(newPoints, double(0.), double(0.), double(gridWidth), double(gridHeight));
            if (x & 1)
                std::reverse(newPoints.begin(), newPoints.end());
        }
    }
    if ((curveType & 2) != 0) {
        for (size_t y = 0; y <= gridHeight; ++y) {
            points.push_back(Pointfs());
            Pointfs &newPoints = points.back();
            newPoints = zip(
                colinearPoints(offset, 0, gridWidth),
                perpendPoints(offset, y, gridWidth));
            // trim points to grid edges
            trim(newPoints, double(0.), double(0.), double(gridWidth), double(gridHeight));
            if (y & 1)
                std::reverse(newPoints.begin(), newPoints.end());
        }
    }
    return points;
}

// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron with a specified
// grid square size.
static Polylines makeClassicGrid(coord_t z, coord_t gridSize, size_t gridWidth, size_t gridHeight, size_t curveType)
{
    coord_t  scaleFactor = gridSize;
    double normalisedZ = double(z) / double(scaleFactor);
    std::vector<Pointfs> polylines = makeNormalisedGrid(normalisedZ, gridWidth, gridHeight, curveType);
    Polylines result;
    result.reserve(polylines.size());
    for (std::vector<Pointfs>::const_iterator it_polylines = polylines.begin(); it_polylines != polylines.end(); ++ it_polylines) {
        result.push_back(Polyline());
        Polyline &polyline = result.back();
        for (Pointfs::const_iterator it = it_polylines->begin(); it != it_polylines->end(); ++ it)
            polyline.points.push_back(Point(coord_t((*it)(0) * scaleFactor), coord_t((*it)(1) * scaleFactor)));
    }
    return result;
}

// 3.0's 3D Honeycomb ("3D Honeycomb (classic)")
void Fill3DHoneycombClassic::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    // no rotation is supported for this infill pattern
    BoundingBox bb = Algorithms::Polygon::get_bounding_box(expolygon.contour);
    coord_t     distance = coord_t(scale_(this->spacing) / params.density);

    // align bounding box to a multiple of our honeycomb grid module
    // (a module is 2*$distance since one $distance half-module is 
    // growing while the other $distance half-module is shrinking)
    bb = BB::merge(bb, align_to_grid(bb.min, Point(2*distance, 2*distance)));
    
    // generate pattern
    Polylines   polylines = makeClassicGrid(
        scale_(this->z),
        distance,
        ceil(BB::sizes(bb)(0) / distance) + 1,
        ceil(BB::sizes(bb)(1) / distance) + 1,
        ((this->layer_id/thickness_layers) % 2) + 1);
    
    // move pattern in place
	for (Polyline &pl : polylines)
		pl.translate(bb.min);

    // clip pattern to boundaries, chain the clipped polylines
    polylines = intersection_pl(polylines, expolygon);

    // connect lines if needed
    if (params.dont_connect() || polylines.size() <= 1)
        append(polylines_out, chain_polylines(std::move(polylines)));
    else
        this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);
}

// Orca's 3D Honeycomb
using coordf_t = double;

// sign function
template <typename T> int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}
  
/*
Creates a contiguous sequence of points at a specified height that make
up a horizontal slice of the edges of a space filling truncated
octahedron tesselation. The octahedrons are oriented so that the
square faces are in the horizontal plane with edges parallel to the X
and Y axes.

Credits: David Eccles (gringer).
*/

// triangular wave function
// this has period (gridSize * 2), and amplitude (gridSize / 2),
// with triWave(pos = 0) = 0
static coordf_t triWave(coordf_t pos, coordf_t gridSize)
{
  float t = (pos / (gridSize * 2.)) + 0.25; // convert relative to grid size
  t = t - (int)t; // extract fractional part
  return((1. - abs(t * 8. - 4.)) * (gridSize / 4.) + (gridSize / 4.));
}

// truncated octagonal waveform, with period and offset
// as per the triangular wave function. The Z position adjusts
// the maximum offset [between -(gridSize / 4) and (gridSize / 4)], with a
// period of (gridSize * 2) and troctWave(Zpos = 0) = 0
static coordf_t troctWave(coordf_t pos, coordf_t gridSize, coordf_t Zpos)
{
  coordf_t Zcycle = triWave(Zpos, gridSize);
  coordf_t perpOffset = Zcycle / 2;
  coordf_t y = triWave(pos, gridSize);
  return((abs(y) > abs(perpOffset)) ?
	 (sgn(y) * perpOffset) :
	 (y * sgn(perpOffset)));
}

// Identify the important points of curve change within a truncated
// octahedron wave (as waveform fraction t):
// 1. Start of wave (always 0.0; not needed if the pattern base starts here)
// 2. Transition to upper "horizontal" part
// 3. Transition from upper "horizontal" part
// 4. Transition to lower "horizontal" part
// 5. Transition from lower "horizontal" part
/*    o---o
 *   /     \
 * o/       \
 *           \       /
 *            \     /
 *             o---o
 */
 static std::vector<coordf_t> getCriticalPoints(coordf_t Zpos, coordf_t gridSize)
{
  std::vector<coordf_t> res;
  coordf_t perpOffset = abs(triWave(Zpos, gridSize) / 2.);
  coordf_t normalisedOffset = perpOffset / gridSize;
  if(normalisedOffset > 0){
    res.push_back(gridSize * (0. + normalisedOffset));
    res.push_back(gridSize * (1. - normalisedOffset));
    res.push_back(gridSize * (1. + normalisedOffset));
    res.push_back(gridSize * (2. - normalisedOffset));
  }
  return(res);
}

// Add additional dense fill in line with the pattern direction to
// cover the top squares of the pattern
static Polylines addTops(coordf_t Zpos, coordf_t gridSize, coordf_t lengthX, coordf_t lengthY, coordf_t spacing,
                         size_t multiline_count, size_t topDistance)
{
  coordf_t zCycle = fmod(Zpos + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  coordf_t zHalfCycle = fmod(zCycle, 0.5) * 2.;
  bool printVert = zCycle < 0.5;
  coordf_t offsetX = multiline_count;
  coordf_t offsetY = multiline_count;
  coordf_t perpOffset = abs(triWave(Zpos, gridSize) / 2.);
  coordf_t gridPoint = gridSize * (0. + perpOffset / gridSize);
  coordf_t topOffset = gridSize / 2.0 - abs(troctWave(gridPoint, gridSize, Zpos));
  coordf_t multilineAdjust = (sqrt(2) - 1.0) / 2.;
  Polylines lines;
  size_t pointCount = 0;
  coordf_t gridStartL = gridSize * 0.5 - topOffset;
  coordf_t gridEndL = gridSize * 0.5 + topOffset;
  if((topDistance == 0) && (multiline_count == 1)){
    // extend out a little bit on the first layer to help fuse the cover
    gridStartL -= spacing;
    gridEndL += spacing;
  } else if(multiline_count > 1) {
    // match start point to the corner edge
    gridStartL -= spacing * multiline_count * multilineAdjust;
    gridEndL += spacing * multiline_count * multilineAdjust;
  }
  // top cover extents perpendicular to the direction of travel
  coordf_t gridStartP = gridSize * 0.5 - topOffset + spacing * multiline_count / 2. + spacing / 2.;
  coordf_t gridEndP = gridSize * 0.5 + topOffset - spacing * multiline_count / 2. - spacing / 2.;
  coordf_t x, y;
  int xm, ym;
  // if the print direction needs to be rotated, then swap the extents
  if((topDistance % 2) == 0){
    std::swap(gridStartL, gridStartP);
    std::swap(gridEndL, gridEndP);
  }
  // adjust spacing so that it starts and ends on exactly the right place
  // and increase fill density slightly to reduce gaps
  coordf_t region_count = floor((gridEndP - gridStartP) / (spacing / sqrt(2)));
  if(region_count <= 0){
    return lines;
  }
  spacing = (gridEndP - gridStartP) / region_count;
  for (x = offsetX, xm = 0; x <= (lengthX); x+= gridSize, xm = xm ^ 1) {
    for (y = offsetY, ym = 0; y <= (lengthY); y += gridSize, ym = ym ^ 1) {
      if(((xm ^ ym) == 1) == printVert){
        continue;
      }
      // // For debugging: remove 0,0 -> 1,1 top to help understand orientation
      // if((x <= (gridSize + EPSILON)) && (y <= (gridSize + EPSILON)) && ((y - x) < EPSILON)){
      //   continue;
      // }
      Polyline newPoints;
      int dirMod = xm ^ ym;
      if(printVert == (topDistance % 2)){
        if(y < (lengthY - spacing * multiline_count * 1.5)){
          coordf_t endPMod = std::min(lengthX - (multiline_count * (spacing + 1) / 2.), x + gridEndP) - x;
          coordf_t endLMod = std::min(lengthY - (multiline_count * (spacing + 1) / 2.), y + gridEndL) - y;
          for(coordf_t xi = gridStartP; xi < (endPMod + EPSILON); xi += spacing, dirMod = dirMod ^ 1){
            newPoints.points.push_back((dirMod == 0) ? Point(x + xi, y + gridStartL) : Point(x + xi, y + endLMod));
            newPoints.points.push_back((dirMod == 0) ? Point(x + xi, y + endLMod) : Point(x + xi, y + gridStartL));
            pointCount += 2;
          }
        }
      } else {
        if(x < (lengthX - spacing * multiline_count * 1.5)){
          coordf_t endPMod = std::min(lengthY - (multiline_count * (spacing + 1) / 2.), y + gridEndP) - y;
          coordf_t endLMod = std::min(lengthX - (multiline_count * (spacing + 1) / 2.), x + gridEndL) - x;
          for(coordf_t yi = gridStartP; yi < (endPMod + EPSILON); yi += spacing, dirMod = dirMod ^ 1){
            newPoints.points.push_back((dirMod == 0) ? Point(x + gridStartL, y + yi) : Point(x + endLMod, y + yi));
            newPoints.points.push_back((dirMod == 0) ? Point(x + endLMod, y + yi) : Point(x + gridStartL, y + yi));
            pointCount += 2;
          }
        }
      }
      lines.push_back(newPoints);
    }
  }
  return lines;
}

// Generate a set of polylines that complete octahedron curves on the
// extremities of a pattern
static Polylines makeEndPoints(const coordf_t Zpos, coordf_t gridSize, std::vector<coordf_t> critPoints,
                               coordf_t lengthX, coordf_t lengthY, coordf_t spacing, size_t multiline_count)
{
  Polylines lines;
  coordf_t zCycle = fmod(Zpos + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  bool printVert = zCycle < 0.5;
  bool printHoriz = zCycle >= 0.5;
  int zFlipOffset = ((sgn(fmod(zCycle, 0.5) - 0.25) > 0) == printVert) ? 0 : 1;
  // create templates for copying
  Polylines startLines, endLines;
  for(size_t li = 0; li < multiline_count; li++){
    coordf_t oAdj = (li - ((multiline_count - 1) / 2.)) * spacing; // orthogonal line adjustment
    coordf_t dAdj = oAdj * sqrt(2); // diagonal line adjustment
    Polyline startLine, endLine;
    // Left Bottom; Bottom Left
    startLine.points.push_back(printHoriz ? Point(oAdj, -dAdj) : Point(-dAdj, oAdj));
    // Right Bottom; Top Left
    endLine.points.push_back(printHoriz ? Point(-oAdj, -dAdj) : Point(-dAdj, -oAdj));
    for(size_t pi = 0; pi < 2; pi++){
      int pDir = pi * 2 - 1;
      coordf_t pAdj = pDir * (sqrt(2) - 1) * oAdj;
      coordf_t troctOffset = abs(troctWave(critPoints[pi], gridSize, Zpos));
      startLine.points.push_back(printHoriz ?
                                 Point(-troctOffset, critPoints[pi] + pAdj) :
                                 Point(critPoints[pi] + pAdj, -troctOffset));
      endLine.points.push_back(printHoriz ?
                               Point(troctOffset, critPoints[pi] + pAdj) :
                               Point(critPoints[pi] + pAdj,  troctOffset));
    }
    // Left Top; Bottom Right
    startLine.points.push_back(printHoriz ? Point(oAdj, gridSize + dAdj) : Point(gridSize + dAdj, oAdj));
    // Right Top; Top Right
    endLine.points.push_back(printHoriz ? Point(-oAdj, gridSize + dAdj) : Point(gridSize + dAdj, -oAdj));
    startLines.push_back(startLine);
    endLines.push_back(endLine);
  }
  coordf_t gridMaxX = ceil((lengthX - EPSILON) / gridSize) * gridSize;
  coordf_t gridMaxY = ceil((lengthY - EPSILON) / gridSize) * gridSize;
  for(size_t li = 0; li < multiline_count; li++){
    coordf_t mlFactor = (li - ((multiline_count - 1) / 2.)) * spacing;
    for (coordf_t cLoc = zFlipOffset * gridSize; cLoc < ((printHoriz ? gridMaxY : gridMaxX) - EPSILON); cLoc += gridSize * 2) {
      Polyline tsLine(startLines[li]);
      Polyline teLine(endLines[li]);
      tsLine.translate(printVert ? Point(cLoc, -mlFactor) : Point(-mlFactor, cLoc));
      teLine.translate(printVert ? Point(cLoc, gridMaxY + mlFactor) : Point(gridMaxX + mlFactor, cLoc));
      lines.push_back(tsLine);
      lines.push_back(teLine);
    }
  }
  return lines;
}

// Generate a polyline that describes a single path segment through
// the infill in the same direction as the basic printing line (i.e. X
// points for columns, Y points for rows)
static Polyline patternPoints(const coordf_t Zpos, coordf_t gridSize, std::vector<coordf_t> critPoints,
                              coordf_t gridLength, coordf_t perpDir, int print_dir, coordf_t oAdj)
{
  Polyline line;
  coordf_t dAdj = oAdj * (sqrt(2) - 1); // additional diagonal adjustment
  coordf_t zCycle = fmod(Zpos + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  int zFlipDirection = sgn(fmod(zCycle, 0.5) - 0.25);
  bool hitEnd = false;
  int endPi = -1;
  size_t pi = 0;
  size_t piOfs = 0;
  line.points.push_back((print_dir == 1) ? Point(dAdj, 0.) : Point(0., dAdj));
  coordf_t gridMax = ceil((gridLength - EPSILON) / gridSize) * gridSize;
  for (coordf_t cLoc = 0; cLoc < gridMax; cLoc += gridSize, piOfs = (piOfs + 2) % 4) {
    for(pi = piOfs; pi < (piOfs + 2); pi++){
      coordf_t offset = troctWave(critPoints[pi], gridSize, Zpos);
      coordf_t offsetFlip = sgn(offset);
      coordf_t posFlip = floor(((pi + 1) % 4) / 2) * 2 - 1;
      coordf_t posLin = cLoc - (piOfs * gridSize / 2.) + critPoints[pi];
      coordf_t posPerp = offset * perpDir;
      line.points.push_back((print_dir == 1) ?
                            Point(posPerp, posLin + posFlip * dAdj * perpDir * zFlipDirection * print_dir) :
                            Point(posLin + posFlip * dAdj * perpDir * zFlipDirection * print_dir, posPerp));
    }
  }
  line.points.push_back((print_dir == 1) ? Point(dAdj, gridMax) : Point(gridMax, dAdj));
  return line;
}

// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron.
  static Polylines makeZigZag(coordf_t Zpos, coordf_t gridSize, coordf_t lengthX, coordf_t lengthY,
                              coordf_t spacing, size_t multiline_count)
{
  Polylines lines;
  std::vector<coordf_t> critPoints = getCriticalPoints(Zpos, gridSize);
  coordf_t zCycle = fmod(Zpos + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  bool printVert = zCycle < 0.5;
  int perpDir = -1;
  int perpDirPattern = -1;
  coordf_t gridMax = ceil(((printVert ? lengthX : lengthY) - EPSILON) / gridSize) * gridSize;
  for (coordf_t pPos = 0; pPos < gridMax; pPos += gridSize, perpDirPattern *= -1) {
    for (size_t li = 0; li < multiline_count; li++){
      coordf_t oAdj = (li - ((multiline_count - 1) / 2.)) * spacing; // orthogonal line adjustment
      Polyline newPoints;
      newPoints = patternPoints(Zpos, gridSize, critPoints,
                                printVert ? lengthY : lengthX,
                                perpDirPattern, printVert ? 1 : -1, oAdj);
      if (perpDir == 1)
        std::reverse(newPoints.points.begin(), newPoints.points.end());
      newPoints.translate(printVert ? Point(pPos + oAdj, 0.) : Point(0., pPos + oAdj));
      lines.push_back(newPoints);
      perpDir *= -1;
    }
  }
  return lines;
}

// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron with a specified
// grid square size.
// gridWidth and gridHeight define the width and height of the bounding box respectively
// Note: this uses the 'complete' infill parameter to determine if the
//       square tops should be enclosed (true) or open (false). Alternatively,
//       a rotation angle of 180 degrees or greater can be used.
static Polylines makeGrid(coordf_t z, coordf_t zLast, coordf_t gridSize,
                          coordf_t lengthX, coordf_t lengthY,
                          bool completeTops, coordf_t spacing, size_t multiline_count, size_t layer_count)
{
  coordf_t zCycle = fmod(z + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  bool printVert = zCycle < 0.5;
  coordf_t zCycleLast = fmod(zLast + gridSize/2, gridSize * 2.) / (gridSize * 2.);
  bool printVertLast = zCycleLast < 0.5;
  Polylines result;
  Polylines polyZag = makeZigZag(z, gridSize, lengthX, lengthY, spacing, multiline_count);
  result.insert(result.end(), polyZag.begin(), polyZag.end());
  // add end connectors
  std::vector<coordf_t> critPoints = getCriticalPoints(z, gridSize);
  Polylines endPoints = makeEndPoints(z, gridSize, critPoints, lengthX, lengthY, spacing, multiline_count);
  result.insert(result.end(), endPoints.begin(), endPoints.end());
  // add tops for the first <multiline_count> layers in each cycle
  if(completeTops && (printVert != printVertLast)){
    coordf_t layerHeight = (z - zLast) / (multiline_count * layer_count);
    size_t top_distance = 0;
    for(coordf_t zCheck = z; zCheck >= (zLast + EPSILON); zCheck -= layerHeight * layer_count, top_distance++){
      coordf_t zCheckCycle = fmod(zCheck + gridSize/2, gridSize * 2.) / (gridSize * 2.);
      if(printVert != (zCheckCycle < 0.5)){
        break;
      }
    }
    Polylines polytops = addTops(z, gridSize, lengthX, lengthY, spacing, multiline_count, top_distance);
    result.insert(result.end(), polytops.begin(), polytops.end());
  }
  return result;
}

// FillParams has the following useful information:
// density <0 .. 1>  [proportion of space to fill]
// dont_connect()    [avoid connect lines]
// dont_adjust       [avoid filling space evenly]
// monotonic         [fill strictly left to right]
// complete          [complete each loop]
// multiline         [number of lines to draw for each pattern line]
// complete_top      [should the top surfaces of the pattern be filled]

void Fill3DHoneycomb::_fill_surface_single(
    const FillParams                &params,
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction,
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    // Support infill angle 
    auto infill_angle   = float(this->angle);
    if (std::abs(infill_angle) >= EPSILON) expolygon.rotate(-infill_angle);
    BoundingBox bb = Algorithms::Polygon::get_bounding_box(expolygon.contour);

    // Increase the bounding box outwards to avoid edge clipping artefacts
    coord_t expandSize = 5. * scale_(this->spacing);
    bb = BB::inflated(bb, expandSize);

    // Adjustment for combining infill setting
    size_t layersPerSlice = 1;
    if(thickness_layers > 0){
      layersPerSlice = thickness_layers;
    }

    // Note: with equally-scaled X/Y/Z, the pattern will create a vertically-stretched
    // truncated octahedron; so Z is pre-adjusted first by scaling by sqrt(2)
    coordf_t zScale = sqrt(2);

    // Density adjustment to account for the additional distance of
    // octagram curves. [This only strictly applies for a rectangular
    // area where the total Z travel distance is a multiple of the
    // spacing]
    // = 4 * integrate(func=4*x(sqrt(2) - 1) + 1, from=0, to=0.25)
    // = (sqrt(2) + 1) / 2 [... I think]
    // make a first guess at the preferred grid Size (in unscaled units)
    coordf_t gridSize = (scale_(this->spacing) *
                         ((zScale + 1.) / 2.) * params.multiline  / params.density);
    coordf_t layerHeight = scale_(params.layer_height);
    coordf_t layersPerModule = floor((gridSize * 2) / (zScale * layerHeight) + 0.05);
    // If a density over 42% is requested, set an exact layer pattern
    if((params.density > 0.42) || (layersPerModule < 2)){
      layersPerModule = 2;
      // re-adjust the grid size for a partial octahedral path
      // (scale of 1.1 guessed based on modeling)
      gridSize = (scale_(this->spacing) * 1.1 * params.multiline  / params.density);
      // re-adjust zScale to make layering consistent
      zScale = (gridSize * 2) / (layersPerModule * layerHeight);
    }

    // align bounding box to a multiple of the octahedron grid so that
    // layers with different starting points have matching origins
    bb = BB::merge(bb, align_to_grid(bb.min, Point(gridSize * 2., gridSize * 2.)));

    // Z adjustment to start at the widest point for the lowest layer
    coordf_t startOffset = gridSize / 2. + scale_(params.layer_height / 2.);
    // generate pattern
    Polylines polylines =
      makeGrid(
	       scale_(this->z) * zScale + startOffset,
	       scale_(this->z - (params.layer_height * params.multiline * layersPerSlice)) * zScale + startOffset,
	       gridSize, BB::sizes(bb)(0), BB::sizes(bb)(1),
	       params.infill_complete_top,
               scale_(this->spacing),
               params.multiline,
               layersPerSlice);

    // move pattern in place
    for (Polyline &pl : polylines){
      pl.translate(bb.min);
      Algorithms::Polyline::simplify(pl, 5 * spacing); // simplify to 5x line width
      // Orca: round the corners of the octahedral wave. The layers where the wave degenerates to a
      // straight line have no corner to round.
      smooth_polyline_corners(pl, params.smooth_factor, scaled<double>(params.resolution));
    }

    // Note: multiline fill adjustment is carried out in this code,
    // rather than using the multiline_fill function

    // clip pattern to boundaries, chain the clipped polylines
    polylines = intersection_pl(std::move(polylines), Algorithms::ExPolygon::to_polygons(expolygon));

    if (! polylines.empty()) {
    // Remove very small bits, but be careful to not remove infill lines connecting thin walls!
    // The infill perimeter lines should be separated by around a single infill line width.
    const double minlength = scale_(0.8 * this->spacing);
    polylines.erase(
	std::remove_if(polylines.begin(), polylines.end(), [minlength](const Polyline &pl) { return Algorithms::Polyline::length(pl.points) < minlength; }),
	polylines.end());
    }

    // copy from fliplines
    if (!polylines.empty()) {
        int infill_start_idx = polylines_out.size(); // only rotate what belongs to us.
        // connect lines
        if (params.dont_connect())
            append(polylines_out, chain_polylines(std::move(polylines)));
        else
            this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

        // rotate back
        if (std::abs(infill_angle) >= EPSILON) {
          for (auto it = polylines_out.begin() + infill_start_idx; it != polylines_out.end(); ++it) 
            it->rotate(infill_angle);
        }
    }
}

} // namespace Slic3r

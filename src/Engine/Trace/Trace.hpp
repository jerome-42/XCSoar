/* Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009

	M Roberts (original release)
	Robin Birch <robinb@ruffnready.co.uk>
	Samuel Gisiger <samuel.gisiger@triadis.ch>
	Jeff Goodenough <jeff@enborne.f2s.com>
	Alastair Harrison <aharrison@magic.force9.co.uk>
	Scott Penrose <scottp@dd.com.au>
	John Wharington <jwharington@gmail.com>
	Lars H <lars_hn@hotmail.com>
	Rob Dunning <rob@raspberryridgesheepfarm.com>
	Russell King <rmk@arm.linux.org.uk>
	Paolo Ventafridda <coolwind@email.it>
	Tobias Lohner <tobias@lohner-net.de>
	Mirek Jezek <mjezek@ipplc.cz>
	Max Kellermann <max@duempel.org>
	Tobias Bieniek <tobias.bieniek@gmx.de>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
 */
#ifndef TRACE_HPP
#define TRACE_HPP

#include "Util/NonCopyable.hpp"
#include <kdtree++/kdtree.hpp>
#include "Navigation/TracePoint.hpp"
#include "Navigation/TaskProjection.hpp"
#include <set>
#include <list>
#include <map>

class AIRCRAFT_STATE;

typedef std::set<TracePoint, TracePoint::time_sort> TracePointSet;
typedef std::list<TracePoint> TracePointList;

/**
 * Container for traces using kd-tree representation internally for fast 
 * geospatial lookups.
 *
 * @todo consider partial thinning, maintenance of separate stores or some
 *       kind of LOD algorithm; currently this does slow down on long flights
 */
class Trace: private NonCopyable 
{
public:
  /**
   * Constructor.  Task projection is updated after first call to append().
   *
   */
  Trace();

  /**
   * Add trace to internal store.  Call optimise() periodically
   * to balance tree for faster queries
   *
   * @param state Aircraft state to log point for
   */
  void append(const AIRCRAFT_STATE& state);

  /**
   * Optimise the internal search tree after adding/removing elements.
   */
  void optimise();

  /**
   * Clear the trace store
   *
   */
  void clear();

  /**
   * Size of traces (in tree, not in temporary store) ---
   * must call optimise() before this for it to be accurate.
   *
   * @return Number of traces in tree
   */
  unsigned size() const;

  /**
   * Whether traces store is empty
   *
   * @return True if no traces stored
   */
  bool empty() const;

  /**
   * Type of KD-tree data structure for trace container
   */
  typedef KDTree::KDTree<2, TracePoint, TracePoint::kd_get_location> TraceTree;

  /**
   * Access first trace in store, for use in iterators.
   *
   * @return First trace in store
   */
  TraceTree::const_iterator begin() const;

  /**
   * Access end trace in store, for use in iterators as end point.
   *
   * @return End trace in store
   */
  TraceTree::const_iterator end() const;

  /**
   * Re-balance kd-tree periodically 
   *
   * @return True if trace store was optimised
   */
  bool optimise_if_old();

  /**
   * Find traces within approximate range (square range box)
   * to search location.  Possible use by screen display functions.
   *
   * @param loc Location from which to search
   * @param range Distance in meters of search radius
   * @param mintime Minimum time to match (recency)
   * @param resolution Thin data to achieve minimum step size in (m) (if positive)
   *
   * @return Vector of trace points within square range
   */
  TracePointVector
  find_within_range(const GEOPOINT &loc, const fixed range, const unsigned mintime=0,
                    const fixed resolution = -fixed_one) const;

  /** 
   * Retrieve a vector of trace points thinned to limit the number of points 
   * 
   * @param max_points Maximum number of points to thin to
   * 
   * @return Vector of trace points
   */
  TracePointVector get_trace_points(const unsigned max_points) const;

  typedef std::map<unsigned, unsigned> TraceDeltaMap;

private:
  void thin_trace(TracePointList& vec, const unsigned range_sq) const;
  void trim_point();
  unsigned lowest_delta() const;
  bool recent(unsigned time) const;

  TraceTree trace_tree;
  TraceDeltaMap distance_delta_map;
  TraceDeltaMap time_delta_map;

  TaskProjection task_projection;

  TracePoint m_last_point;
  unsigned m_optimise_time;

  void erase(TraceTree::const_iterator& it);

  /**
   * Find item which precedes this item
   *
   * @return Iterator to item
   */
  TraceTree::const_iterator find_prev(const TracePoint& tp) const;

  /**
   * Find item which follows this item
   *
   * @return Iterator to item
   */
  TraceTree::const_iterator find_next(const TracePoint& tp) const;

  void update_delta(TraceTree::const_iterator it_prev,
                    TraceTree::const_iterator it,
                    TraceTree::const_iterator it_next);

public:
#ifdef DO_PRINT
  void print(const GEOPOINT &loc) const;
#endif
};

#endif

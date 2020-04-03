// Copyright 2020 The TensorStore Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TENSORSTORE_INTERNAL_GRID_PARTITION_IMPL_H_
#define TENSORSTORE_INTERNAL_GRID_PARTITION_IMPL_H_

/// \file
/// Implementation details of grid_partition.h exposed for testing.

// IWYU pragma: private, include "third_party/tensorstore/internal/grid_partition.h"

#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/container/inlined_vector.h"
#include "absl/types/optional.h"
#include "tensorstore/array.h"
#include "tensorstore/index.h"
#include "tensorstore/index_space/index_transform.h"
#include "tensorstore/util/iterate.h"
#include "tensorstore/util/span.h"
#include "tensorstore/util/status.h"

namespace tensorstore {
namespace internal_grid_partition {

/// Precomputed data structure generated by
/// PrePartitionIndexTransformOverRegularGrid that permits efficient iteration
/// over the grid cell index vectors and corresponding `cell_transform` values.
///
/// Logically, it is simply a container for a list `index_array_sets` of
/// IndexArraySet objects and a list `strided_sets` of StridedSet objects that
/// contain the precomputed data for all of the connected sets of input and grid
/// dimensions.  The order of the sets within these lists is not important for
/// correctness, although it does affect the iteration order.
class IndexTransformGridPartition {
 public:
  /// Represents a connected set containing only `single_input_dimension` edges
  /// within an IndexTransformGridPartition data structure.
  ///
  /// By definition, such connected sets must contain only a single input
  /// dimension.
  ///
  /// No precomputed data is required, as it is possible to efficiently iterate
  /// directly over the partitions.
  struct StridedSet {
    span<const DimensionIndex> grid_dimensions;
    DimensionIndex input_dimension;
  };

  /// Represents a connected set containing `array` edges within an
  /// IndexTransformGridPartition data structure.
  ///
  /// The following information is precomputed:
  ///
  /// - the list `Hs` of partial grid cell index vectors;
  ///
  /// - for each partial grid cell, index arrays that provide a one-to-one map
  ///   from a synthetic one-dimensional space to the domains of each of the
  ///   input dimensions in the connected set.
  struct IndexArraySet {
    /// The grid dimension indices in this set.
    span<const DimensionIndex> grid_dimensions;

    /// The input dimension indices in this set.
    span<const DimensionIndex> input_dimensions;

    // TODO(jbms): Consider using absl::InlinedVector for `grid_cell_indices`
    // and `grid_cell_partition_offsets`.

    /// Row-major array of shape `[num_grid_cells, grid_dimensions.size()]`.
    /// Logically, this is a one-dimensional array of partial grid cell index
    /// vectors, sorted lexicpgrahically with respect to the components of the
    /// vectors (the second dimension of the array).  Each grid cell `i`
    /// corresponds to:
    ///
    ///     partitioned_input_indices[grid_cell_partition_offsets[i]:
    ///                               grid_cell_partition_offsets[i+1], :]
    std::vector<Index> grid_cell_indices;

    /// Array of partial input index vectors corresponding to the partial input
    /// domain of this connected set.  The vectors are partitioned by their
    /// corresponding partial grid cell index vector.  The shape is
    /// `[num_positions,input_dimensions.size()]`.
    SharedArray<Index, 2> partitioned_input_indices;

    /// Specifies the index into the first dimension of
    /// partitioned_input_indices for each grid cell in `grid_cell_indices`.
    /// Array of shape `[num_grid_cells]`.
    std::vector<Index> grid_cell_partition_offsets;

    /// Returns the index vector array of shape
    /// `{num_positions_in_partition, input_dim}` that maps the synthetic
    /// one-dimensional space to the domains of each input dimension.
    SharedArray<const Index, 2> partition_input_indices(
        Index partition_i) const;

    /// Returns the partial grid cell index vector corresponding to partition
    /// `i`.
    ///
    /// \returns A `span` of size `grid_dimensions.size()`.
    span<const Index> partition_grid_cell_indices(Index partition_i) const;

    /// Returns the number of partitions (partial grid cell index vectors).
    Index num_partitions() const {
      return static_cast<Index>(grid_cell_partition_offsets.size());
    }
  };

  span<const IndexArraySet> index_array_sets() const {
    return index_array_sets_;
  }

  span<const StridedSet> strided_sets() const { return strided_sets_; }

  /// The following members should be treated as private.
  explicit IndexTransformGridPartition(DimensionIndex input_rank,
                                       DimensionIndex grid_rank);

  /// Prohibit copying and moving, as the default implementation would
  /// invalidate the `grid_dimensions` and `input_dimensions` arrays of the
  /// contained `IndexArraySet` and `StridedSet` sub-objects, and copying isn't
  /// needed anyway.
  IndexTransformGridPartition(const IndexTransformGridPartition&) = delete;
  IndexTransformGridPartition(IndexTransformGridPartition&&) = delete;

  /// Buffer of size `input_rank + grid_rank` that holds the input dimension
  /// indices and grid dimension indices that are referenced by the
  /// `input_dimensions` and `grid_dimensions` members of the IndexArraySet and
  /// StridedSet sub-objects.
  absl::FixedArray<DimensionIndex, internal::kNumInlinedDims * 2> temp_buffer_;

  /// Precomputed data for the strided connected sets.
  absl::InlinedVector<StridedSet, internal::kNumInlinedDims> strided_sets_;

  /// Precomputed data for the index array connected sets.
  absl::InlinedVector<IndexArraySet, internal::kNumInlinedDims>
      index_array_sets_;
};

/// Precomputes a data structure for partitioning an index transform by a
/// regular multi-dimensional grid.
///
/// The grid is a multi-dimensional grid with the specified `grid_cell_shape`.
/// The mapping from grid dimensions to output dimensions of the index transform
/// is specified by the `grid_output_dimensions` array.  The grid extends from
/// -inf to +inf over all grid dimensions.  The grid cell with index vector `v`
/// corresponds to the hyperrectangle with inclusive lower bound `v *
/// grid_cell_shape` and exclusive upper bound `(v + 1) * grid_cell_shape`.
///
/// \param index_transform The index transform to partition.
/// \param grid_output_dimensions The sequence of output dimensions of
///     `index_transform` corresponding to the grid over which the index
///     transform is to be partitioned.
/// \param grid_cell_shape The shape of a grid cell.
/// \param result[out] Non-null pointer to result.
/// \error `absl::StatusCode::kInvalidArgument` if any input dimension of
///     `index_transform` has an unbounded domain.
/// \error `absl::StatusCode::kInvalidArgument` if integer overflow occurs.
/// \error `absl::StatusCode::kOutOfRange` if an index array contains an
///     out-of-bounds index.
Status PrePartitionIndexTransformOverRegularGrid(
    IndexTransformView<> index_transform,
    span<const DimensionIndex> grid_output_dimensions,
    span<const Index> grid_cell_shape,
    absl::optional<IndexTransformGridPartition>* result);

}  // namespace internal_grid_partition
}  // namespace tensorstore

#endif  // TENSORSTORE_INTERNAL_GRID_PARTITION_IMPL_H_

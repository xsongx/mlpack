/**
 * @file allknn_main.cpp
 * @author Ryan Curtin
 *
 * Implementation of the AllkNN executable.  Allows some number of standard
 * options.
 */
#include <mlpack/core.hpp>

#include <string>
#include <fstream>
#include <iostream>

#include "neighbor_search.hpp"

using namespace std;
using namespace mlpack;
using namespace mlpack::neighbor;
using namespace mlpack::tree;

// Information about the program itself.
PROGRAM_INFO("All K-Nearest-Neighbors",
    "This program will calculate the all k-nearest-neighbors of a set of "
    "points. You may specify a separate set of reference points and query "
    "points, or just a reference set which will be used as both the reference "
    "and query set."
    "\n\n"
    "For example, the following will calculate the 5 nearest neighbors of each"
    "point in 'input.csv' and store the distances in 'distances.csv' and the "
    "neighbors in 'neighbors.csv':"
    "\n\n"
    "$ allknn --k=5 --reference_file=input.csv --distances_file=distances.csv\n"
    "  --neighbors_file=neighbors.csv"
    "\n\n"
    "The output files are organized such that row i and column j in the "
    "neighbors output file corresponds to the index of the point in the "
    "reference set which is the i'th nearest neighbor from the point in the "
    "query set with index j.  Row i and column j in the distances output file "
    "corresponds to the distance between those two points.");

// Define our input parameters that this program will take.
PARAM_STRING_REQ("reference_file", "File containing the reference dataset.",
    "R");
PARAM_STRING("query_file", "File containing query points (optional).", "Q", "");
PARAM_STRING_REQ("distances_file", "File to output distances into.", "D");
PARAM_STRING_REQ("neighbors_file", "File to output neighbors into.", "N");

PARAM_INT("leaf_size", "Leaf size for tree building.", "L", 20);
PARAM_FLAG("naive", "If true, O(n^2) naive mode is used for computation.", "");
PARAM_FLAG("single_mode", "If true, single-tree search is used (as opposed to "
    "dual-tree search.", "S");
PARAM_INT_REQ("k", "Number of furthest neighbors to find.", "");

int main(int argc, char *argv[])
{
  // Give CLI the command line parameters the user passed in.
  CLI::ParseCommandLine(argc, argv);

  // Get all the parameters.
  string referenceFile = CLI::GetParam<string>("reference_file");
  string outputFile = CLI::GetParam<string>("output_file");

  string distancesFile = CLI::GetParam<string>("distances_file");
  string neighborsFile = CLI::GetParam<string>("neighbors_file");

  int leafSize = CLI::GetParam<int>("leaf_size");

  size_t k = CLI::GetParam<int>("k");

  bool naive = CLI::HasParam("naive");
  bool singleMode = CLI::HasParam("single_mode");

  arma::mat referenceData;
  if (!data::Load(referenceFile.c_str(), referenceData))
    Log::Fatal << "Reference file " << referenceFile << "not found." << endl;

  Log::Info << "Loaded reference data from " << referenceFile << endl;

  // Sanity check on k value: must be greater than 0, must be less than the
  // number of reference points.
  if ((k <= 0) || (k >= referenceData.n_cols))
  {
    Log::Fatal << "Invalid k: " << k << "; must be greater than 0 and less ";
    Log::Fatal << "than the number of reference points (";
    Log::Fatal << referenceData.n_cols << ")." << endl;
  }

  // Sanity check on leaf size.
  if (leafSize < 0)
  {
    Log::Fatal << "Invalid leaf size: " << leafSize << ".  Must be greater "
        "than or equal to 0." << endl;
  }

  // Naive mode overrides single mode.
  if (singleMode && naive)
  {
    Log::Warn << "--single_mode ignored because --naive is present." << endl;
  }

  arma::Mat<size_t> neighbors;
  arma::mat distances;

  // Because we may construct it differently, we need a pointer.
  AllkNN* allknn = NULL;

  // Mappings for when we build the tree.
  std::vector<size_t> oldFromNewRefs;

  // Build trees by hand, so we can save memory: if we pass a tree to
  // NeighborSearch, it does not copy the matrix.
  Log::Info << "Building reference tree..." << endl;
  Timer::Start("tree_building");

  BinarySpaceTree<bound::HRectBound<2>, QueryStat<NearestNeighborSort> >
      refTree(referenceData, oldFromNewRefs, leafSize);

  Timer::Stop("tree_building");

  std::vector<size_t> oldFromNewQueries;

  if (CLI::GetParam<string>("query_file") != "")
  {
    string queryFile = CLI::GetParam<string>("query_file");
    arma::mat queryData;

    if (!data::Load(queryFile.c_str(), queryData))
      Log::Fatal << "Query file " << queryFile << " not found" << endl;

    Log::Info << "Query data loaded from " << queryFile << endl;

    Log::Info << "Building query tree..." << endl;

    // Build trees by hand, so we can save memory: if we pass a tree to
    // NeighborSearch, it does not copy the matrix.
    Timer::Start("tree_building");

    BinarySpaceTree<bound::HRectBound<2>, QueryStat<NearestNeighborSort> >
        queryTree(queryData, oldFromNewRefs, leafSize);

    Timer::Stop("tree_building");

    allknn = new AllkNN(referenceData, queryData, naive, singleMode, 20,
        &refTree, &queryTree);

    Log::Info << "Tree built." << endl;
  }
  else
  {
    allknn = new AllkNN(referenceData, naive, singleMode, 20, &refTree);

    Log::Info << "Trees built." << endl;
  }

  Log::Info << "Computing " << k << " nearest neighbors..." << endl;
  allknn->Search(k, neighbors, distances);

  Log::Info << "Neighbors computed." << endl;

  // We have to map back to the original indices from before the tree
  // construction.
  Log::Info << "Re-mapping indices..." << endl;

  arma::mat distancesOut(distances.n_rows, distances.n_cols);
  arma::Mat<size_t> neighborsOut(neighbors.n_rows, neighbors.n_cols);

  // Do the actual remapping.
  for (size_t i = 0; i < distances.n_cols; i++)
  {
    // Map distances (copy a column).
    distancesOut.col(oldFromNewQueries[i]) = distances.col(i);

    // Map indices of neighbors.
    for (size_t j = 0; j < distances.n_rows; j++)
    {
      neighborsOut(j, oldFromNewQueries[i]) = oldFromNewRefs[neighbors(j, i)];
    }
  }

  // Save output.
  data::Save(distancesFile, distances);
  data::Save(neighborsFile, neighbors);

  delete allknn;
}